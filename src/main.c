#include <stdlib.h>
#include <stdio.h>
#include <stdio.h>
#include <poll.h>
#include <stdint.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <json_parse.h>
#include <serial.h>
#include <config_file.h>
#include <debug.h>
#include <termios.h>

struct SerialPort
{
  struct SerialPort *next;
  struct SerialPort **prevp;
  struct pollfd *poll;
  char *path;
  uint16_t decode_buffer;
  uint16_t decode_shift;
  struct AppContext *app;
};

static void
serial_port_destroy(struct SerialPort *port)  
{
  if (port->poll->fd >= 0) {
    close(port->poll->fd);
    port->poll->fd = -1; /* Disable polling */
  }
  /* Unlink */
  if (port->next) {
    port->next->prevp = port->prevp;
  }
  *port->prevp = port->next;
  
  free(port->path);
  free(port);
}
		    
#define MAX_POLL_FDS 8


/* Return true if more data is expected, otherwise it will be removed
   from polling. Called with poll->revents == 0 when destroying callback list.
*/
typedef int (*fd_callback_t)(struct pollfd *poll, void *callback_data);

struct AppContext
{
  struct pollfd pollfds[MAX_POLL_FDS];
  unsigned int n_poll; /* Length of above array */
  fd_callback_t callbacks[MAX_POLL_FDS];
  void *callback_data[MAX_POLL_FDS];

  uint8_t *in_buffer;
  unsigned int in_buffer_capacity;
  unsigned int in_len;
  unsigned int in_pos;
  
  uint8_t *out_buffer;
  unsigned int out_buffer_capacity;
  unsigned int out_len;

  struct SerialPort *serial_ports;
  struct ConfigData *config_data;
};

static void
clear_polls(struct AppContext *app)
{
  unsigned int c;
  for (c = 0; c < app->n_poll; c++) {
    if (app->pollfds[c].fd >= 0) {
      app->pollfds[c].revents = 0;
      app->callbacks[c](&app->pollfds[c], app->callback_data[c]);
    }
  }
  app->n_poll = 0;
}
static void
app_cleanup(struct AppContext *app)
{
  while(app->serial_ports) serial_port_destroy(app->serial_ports);
  clear_polls(app);
  free(app->in_buffer);
  free(app->out_buffer);
}


static struct pollfd*
add_fd(struct AppContext *app, int fd, short events, 
       fd_callback_t callback, void *user_data)
{
  unsigned int i;
  /* Look for an empty slot */
  for (i = 0; i < app->n_poll; i++) {
    if (app->pollfds[i].fd < 0) break;
  }
  if (i >= MAX_POLL_FDS) {
    return NULL;
  }
  app->pollfds[i].fd = fd;
  app->pollfds[i].events = events;
  app->pollfds[i].revents = 0;
  if (i >= app->n_poll) app->n_poll = i + 1;
  app->callbacks[i] = callback;
  app->callback_data[i] = user_data;
  return &app->pollfds[i];
}

int
reply_printf(struct AppContext *app, const char *format, ...)
{
  int w;
  va_list ap;
  va_start(ap, format);
  w = vsnprintf((char*)app->out_buffer+app->out_len, 
		app->out_buffer_capacity - app->out_len,
		format, ap);
  app->out_len += w;
  if (app->out_len >= app->out_buffer_capacity) {
    app->out_len = app->out_buffer_capacity - 1;
  }
  va_end(ap);
  return w;
}

void
reply_append(struct AppContext *app, const char *str)
{
  size_t l = strlen(str);
  if (app->out_len + l >= app->out_buffer_capacity) {
    l = app->out_buffer_capacity - app->out_len - 1;
  }
  memcpy(app->out_buffer + app->out_len,str, l);
  app->out_len += l;
}

static void
reply_send(struct AppContext *app)
{
  uint32_t length = app->out_len;
  fwrite(&length, sizeof(uint32_t), 1, stdout);
  fwrite(app->out_buffer, 1, app->out_len, stdout);
  fflush(stdout);
}

static void 
version_handler(const uint8_t **pp, struct AppContext *app)
{
  reply_append(app,"[\"0.1\"]");
}

static void 
serial_list_handler(const uint8_t **pp, struct AppContext *app)
{
  char **port = app->config_data->serial_ports;
  reply_append(app,"[");
  while(*port) {
    reply_append(app,"\"");
    reply_append(app,*port);
    reply_append(app,"\"");
    port++;
    if (*port) {
      reply_append(app,",");
    }
  }
  reply_append(app,"]");
}

struct SerialOpts default_serial_opts =
  {
    9600,
    4096,
    1,
    8,
    0,
    1
  };

static int
serial_opts_cb(const uint8_t **pp, const char *key, void *cb_data)
{
  struct SerialOpts *opts = cb_data;
  long v;
  if (strcmp(key, "bitRate") == 0) {
    if (!json_parse_int(pp, &v)) {
      PRINTERR("Failed to parse bitRate value\n");
      return 0;
    }
    opts->bitRate = v;
  } else if (strcmp(key, "bufferSize") == 0) {
    if (!json_parse_int(pp, &v)) {
      PRINTERR("Failed to parse bufferSize value\n");
      return 0;
    }
    opts->bufferSize = v;
  } else if (strcmp(key, "ctsFlowControl") == 0) {
    if (!json_parse_int(pp, &v)) {
      PRINTERR("Failed to parse ctsFlowControl value\n");
      return 0;
    }
    opts->ctsFlowControl = v;
  } else if (strcmp(key, "dataBits") == 0) {
    if (!json_parse_int(pp, &v)) {
      PRINTERR("Failed to parse dataBits value\n");
      return 0;
    }
    opts->dataBits = v;
  } else if (strcmp(key, "parityBit") == 0) {
    if (!json_parse_int(pp, &v)) {
      PRINTERR("Failed to parse parityBit value\n");
      return 0;
    }
    opts->parityBit = v;
  } else if (strcmp(key, "stopBits") == 0) {
    if (!json_parse_int(pp, &v)) {
      PRINTERR("Failed to parse stopBits value\n");
      return 0;
    }
    opts->stopBits = v;
  }
  return 1;
}

static struct SerialPort *
find_serial_port_by_path(struct SerialPort *port, const char *path)
{
  while(port) {
    if (strcmp(path, port->path) == 0) return port;
    port = port->next;
  }
  return NULL;
}
static const uint8_t
base64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int
reply_base64(struct AppContext *app, const uint8_t *data, unsigned int len)
{
  uint32_t bits;
  unsigned int left = app->out_buffer_capacity - app->out_len - 1;
  uint8_t *out = app->out_buffer + app->out_len;
  while(len >= 3) {
    bits = (data[0] << 16) | (data[1] << 8) | data[2];
    if (left < 4) return 0;
    *out++ = base64chars[bits >> 18];
    *out++ = base64chars[(bits >> 12) & 0x3f];
    *out++ = base64chars[(bits >> 6) & 0x3f];
    *out++ = base64chars[bits & 0x3f];
    data += 3;
    len -= 3;
    left -= 4;
  }
  if (len > 0 && left < 4) return 0;
  if (len == 2)  {
    bits = (data[0] << 8) | data[1];
    *out++ = base64chars[bits >> 10];
    *out++ = base64chars[(bits >> 4) & 0x3f];
    *out++ = base64chars[(bits << 2) & 0x3f];
    *out++ = '=';
  } else if (len == 1) {
    *out++ = base64chars[data[0] >> 2];
    *out++ = base64chars[(data[0] << 4) & 0x3f];
    *out++ = '=';
    *out++ = '=';
  }
  app->out_len = out - app->out_buffer;
  return 1;
}
	     
static int
serial_recv(struct pollfd *poll, void *cb_data)
{
  uint8_t read_buffer[256];
  size_t r;
  struct SerialPort *port = cb_data;
  struct AppContext *app = port->app;
  if (poll->revents & POLLIN) {
    r = read(poll->fd, read_buffer, sizeof(read_buffer));
    if (r < 0) {
      PRINTERR("Failed to read from %s\n", port->path);
      app->out_len = 0;
      reply_printf(app,
		   "[\"serialError\", \"%s\", \"Failed to read from %s\"]",
		   port->path, port->path);
      reply_send(app);
    } else if (r > 0) {
      app->out_len = 0;
      reply_append(app,"[\"serialRecv\",\"");
      reply_append(app,port->path);
      reply_append(app,"\",\"");
      reply_base64(app, read_buffer, r);
      reply_append(app,"\"]");
      reply_send(app);
      app->out_buffer[app->out_len] = '\0';
      PRINTDEBUG("Serial recv: %s\n", app->out_buffer);
    } else {
      return 0;
    }
  } else if (poll->revents == 0) {
    serial_port_destroy(port);
    return 0;
  }
  return 1;
}

int
parse_path(const uint8_t **pp, struct AppContext *app,
	   char *path, unsigned int len)
{
  if (!json_skip_comma(pp)) {
    PRINTERR("No comma before first parameter");
    reply_append(app, "0");
    return 0;
  }
  if (!json_parse_string_buffer(pp, (uint8_t*)path, len)) {
    PRINTERR("Failed to parse path to serial device");
    reply_append(app, "0");
    return 0;
  }
  return 1;
}

#define CMD_FAIL_RET  reply_append(app, "0");return
static void 
serial_open_raw_handler(const uint8_t **pp, struct AppContext *app)
{
  int fd;
  struct SerialPort *port;
  struct SerialOpts opts = default_serial_opts;
  char path[50];
  
  if (!parse_path(pp, app, path, sizeof(path))) return;
  
  json_skip_white(pp);
  if (**pp == ',') {
    char key[20];
    (*pp)++;
    json_skip_white(pp);
    if (!json_iterate_object(pp,key, sizeof(key), serial_opts_cb, &opts)) {
      CMD_FAIL_RET;
    }
  }
  /* Check if the path is alreay open */
  port = find_serial_port_by_path(app->serial_ports, path);
  if (port) {
    PRINTERR("Serial path already open\n");
    CMD_FAIL_RET;
  }
  fd = serial_open(path, &opts);
  if (fd < 0) {
    CMD_FAIL_RET;
  }
  port = malloc(sizeof(struct SerialPort));
  port->app = app;
  
  if (!port) {
    PRINTERR("No memorty for serial port\n");
    CMD_FAIL_RET;
  }
  
  port->poll = add_fd(app, fd, POLLIN, serial_recv, port);
  if (!port->poll) {
    PRINTERR("No more files allowed\n");
    free(port);
    CMD_FAIL_RET;
   }
  port->path = strdup(path);
  if (!port->path) {
    PRINTERR("No memory for path\n");
    free(port);
    CMD_FAIL_RET;
  }
  
  /* Link port */
  port->prevp = &app->serial_ports;
  port->next = app->serial_ports;
  if (port->next) {
    port->next->prevp = &port->next;
  }
  app->serial_ports = port;
  reply_append(app, "1");
}
static void 
serial_close_handler(const uint8_t **pp, struct AppContext *app)
{
  struct SerialPort *port;
  char path[50];
  
  if (!parse_path(pp, app, path, sizeof(path))) return;

  port = find_serial_port_by_path(app->serial_ports, path);
  if (!port) {
    PRINTERR("Trying to close unopened path: %s\n", path);
    CMD_FAIL_RET;
  }
  serial_port_destroy(port);
  reply_append(app, "1");
}

int
write_all_to_port(struct SerialPort *port,
		  const uint8_t *block, unsigned int len)
{
  while(len > 0) {
    ssize_t written = write(port->poll->fd, block, len);
    if (written < 0) {
      PRINTERR("Failed to write to serial port: %s\n", strerror(errno));
      return 0;
    }
    block += written;
    len -= written;
  }
  return 1;
}
		  
static int
string_writer(const uint8_t *block, unsigned int len, void *cb_data)
{
  struct SerialPort *port = cb_data;
  uint8_t buf[16];
  uint8_t buf_len = 0;
  while(len-- > 0) {
    uint8_t b = *block++;
    uint8_t v;
    if (b >= 'A' && b <= 'Z') {
      v = b - 'A';
    } else  if (b >= 'a' && b <= 'z') {
      v = b - 'a' + 26;
    } else  if (b >= '0' && b <= '9') {
      v = b - '0' + 52;
    }  else  if (b == '+') {
      v = 62;
    } else if (b == '/') {
      v = 63;
    } else {
      continue;
    }
    port->decode_buffer = port->decode_buffer << 6 | v;
    port->decode_shift += 6;
    if (port->decode_shift >= 8) {
      port->decode_shift -= 8;
      buf[buf_len++] = port->decode_buffer >> port->decode_shift;
      if (buf_len == sizeof(buf)) {
	if (!write_all_to_port(port, buf, buf_len)) return 0;
	buf_len = 0;
      }
    }
  }
  if (buf_len > 0) {
    if (!write_all_to_port(port, buf, buf_len)) return 0;
  }
  return 1;
}

static void 
serial_send_raw_handler(const uint8_t **pp, struct AppContext *app)
{
  struct SerialPort *port;
  char path[50];
  if (!parse_path(pp, app, path, sizeof(path))) return;

  port = find_serial_port_by_path(app->serial_ports, path);
  if (!port) {
    PRINTERR("Trying to send to unopened path: %s\n", path);
    CMD_FAIL_RET;

  }
  if (!json_skip_comma(pp)) {
    PRINTERR("No comma after path\n");
    CMD_FAIL_RET;
  }

  port->decode_shift = 0;
  if (!json_parse_string(pp, string_writer, port)) {
    PRINTERR("Failed to write string to serial port\n");
    CMD_FAIL_RET;
  }
  reply_append(app, "1");
}

struct CommandMap
{
  const char *command;
  void (*handler)(const uint8_t **pp, struct AppContext *app);
} command_map [] =
  {
    {"version", version_handler},
    {"serial_list", serial_list_handler},
    {"serial_open_raw", serial_open_raw_handler},
    {"serial_close", serial_close_handler},
    {"serial_send_raw", serial_send_raw_handler},
    {NULL, NULL}
  };



void 
parse_request(const uint8_t *p, struct AppContext *app)
{
  struct CommandMap *cmd = command_map;
  uint8_t token[20];
  uint8_t command[20];
  struct JSONStringBuffer str;
  json_skip_white(&p);
  if (*p != '[') {
    PRINTERR("Request is not an array\n");
  }
  p++;
  str.data = token;
  str.capacity = sizeof(token);
  str.length = 0;
  if (!json_parse_string(&p, json_string_callback, &str)) {
    PRINTERR("Failed to parse request id\n");
    return;
  }
  token[str.length] = '\0';
  if (!json_skip_comma(&p)) {
    PRINTERR("Expected comma\n");
  }
  if (*p != '[') {
    PRINTERR("Message is not an array\n");
  }
  p++;
  str.data = command;
  str.capacity = sizeof(command);
  str.length = 0;
  if (!json_parse_string(&p, json_string_callback, &str)) {
    PRINTERR("Failed to parse command id\n");
    return;
  }
  command[str.length] = '\0';
  PRINTDEBUG("Command: %s\n", command);
  
  while (cmd->command) {
    if (strcmp((const char*)command, cmd->command) == 0) {
      app->out_len = 0;
      reply_printf(app,"[\"@\",\"%s\",", token); 
      cmd->handler(&p, app);
      reply_append(app,"]");
      reply_send(app);
      app->out_buffer[app->out_len] = '\0';
      PRINTDEBUG("Reply: %s\n", app->out_buffer);
      break;
    }
    cmd++;
  }
}

static int
handle_stdin(struct pollfd *poll, void *cb_data)
{
  int r;
  struct AppContext *app = cb_data;
  if (poll->revents == 0) return 0;
  if (app->in_len == 0) {
    r = 4 - app->in_pos;
  } else {
    r = app->in_len - app->in_pos;
  }
  r = read(poll->fd, app->in_buffer + app->in_pos, r);
  if (r == 0) return 0; /* EOF */
  if (r < 0) {
    PRINTERR("Failed to read stdin: %s\n", strerror(errno));
    return 0;
  }
  app->in_pos += r;
  if (app->in_len == 0 && app->in_pos == 4) {
    app->in_len = *(uint32_t*)app->in_buffer;
    if (app->in_len > app->in_buffer_capacity) {
      PRINTERR("Buffer overflow on stdin\n");
      return 0;
    }
    app->in_pos = 0;
  } else if (app->in_pos > 0 && app->in_pos ==  app->in_len) {
    app->in_buffer[app->in_len] = '\0'; /* NUL terminate buffer */
    PRINTDEBUG("'%s'\n", app->in_buffer);
    parse_request(app->in_buffer, app);
    app->in_len = 0;
    app->in_pos = 0;
  }
  return 1;
}


static int exit_pending = 0;

static void
handle_sig(int s)
{
  exit_pending = 1;
}

int
main(int argc, char *argv[])
{
  char conf_filename[200];
  struct AppContext app;
  struct sigaction sig_handler;
  PRINTDEBUG("Device host started\n");
  app.serial_ports = NULL;
  app.config_data = NULL;

  snprintf(conf_filename, sizeof(conf_filename), "%s.json", argv[0]);
  app.config_data = config_data_read(conf_filename);
  if (!app.config_data) {
    PRINTERR("Failed to read configuration file\n");
    return EXIT_FAILURE;
  }
  
  app.in_pos = 0;
  app.in_len = 0;
  app.in_buffer_capacity = 1023;
  app.in_buffer = malloc(app.in_buffer_capacity + 1); /* Make room for NUL */
  if (!app.in_buffer) {
    PRINTERR("No memory for receive buffer\n");
    app_cleanup(&app);
    return EXIT_FAILURE;
  }

  app.out_len = 0;
  app.out_buffer_capacity = 1023;
  app.out_buffer = malloc(app.out_buffer_capacity + 1); /* Make room for NUL */
  if (!app.out_buffer) {
    PRINTERR("No memory for send buffer\n");
    app_cleanup(&app);
    return EXIT_FAILURE;
  }

  app.n_poll = 0;
  
  add_fd(&app, STDIN_FILENO, POLLIN, handle_stdin, &app);
  
  sig_handler.sa_handler = handle_sig;
  sigemptyset(&sig_handler.sa_mask);
  sig_handler.sa_flags = 0;

  sigaction(SIGINT,&sig_handler, NULL);
  sigaction(SIGHUP,&sig_handler, NULL);

  /* Run until stdin is closed */
  while(app.n_poll > 0 && app.pollfds[0].fd >= 0) {
    int n = poll(app.pollfds, app.n_poll, -1);
    if (n < 0) {
      if (errno == EINTR && exit_pending) break;
      PRINTERR("poll failed: %s", strerror(errno));
      app_cleanup(&app);
      return EXIT_FAILURE;
    } else if (n > 0) {
      int p;
      for (p = 0; p < app.n_poll; p++) {
	if (app.pollfds[p].fd >= 0 && app.pollfds[p].revents != 0) {
	  int more;
	  more = app.callbacks[p](&app.pollfds[p], app.callback_data[p]);
	  if (!more) {
	    app.pollfds[p].revents = 0;
	    app.callbacks[p](&app.pollfds[p], app.callback_data[p]);
	    app.pollfds[p].fd = -1;
	    /* Decrease count if this is the last fd */
	    if (p == app.n_poll - 1) app.n_poll--;
	  }
	}
      }
    }
  }
  PRINTDEBUG("Exiting\n");
  app_cleanup(&app);
  return EXIT_SUCCESS;
}
