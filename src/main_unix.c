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
#include <serial_unix.h>
#include <config_file.h>
#include <debug.h>
#include <termios.h>
#include <native_message.h>
#include <scratch_protocol.h>

struct SerialPort
{
  struct SerialPort *next;
  struct SerialPort **prevp;
  struct pollfd *poll;
  char *path;
 
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

  struct NativeMessage nm;
  struct ScratchProtocol sp;

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
  scratch_protocol_destroy(&app->sp);
  native_message_destroy(&app->nm);
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

static struct SerialPort *
find_serial_port_by_path(struct SerialPort *port, const char *path)
{
  while(port) {
    if (strcmp(path, port->path) == 0) return port;
    port = port->next;
  }
  return NULL;
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
      native_message_printf(&app->nm,
			    "[\"serialError\", \"%s\", \"Failed to read from %s\"]",
			    port->path, port->path);
      native_message_send(&app->nm);
    } else if (r > 0) {
      native_message_append_str(&app->nm,"[\"serialRecv\",\"");
      native_message_append_str(&app->nm,port->path);
      native_message_append_str(&app->nm,"\",\"");
      native_message_append_base64(&app->nm, read_buffer, r);
      native_message_append_str(&app->nm,"\"]");
      native_message_send(&app->nm);
      app->nm.out_buffer[app->nm.out_len-4] = '\0';
      PRINTDEBUG("Serial recv: %s\n", app->nm.out_buffer+4);
    } else {
      return 0;
    }
  } else if (poll->revents == 0) {
    serial_port_destroy(port);
    return 0;
  }
  return 1;
}

void
print_req(const uint8_t *msg, unsigned int len, void *context)
{
  printf("Request: '%s'\n", msg);
}

static const char **
unix_serial_get_ports(void *context)
{
  struct AppContext *app = context;
  return (const char**)app->config_data->serial_ports;
}

static int 
unix_serial_open(const char *path, struct SerialOpts *opts,
		 void *context)
{
  int fd;
  struct SerialPort *port;
  struct AppContext *app = context;
    /* Check if the path is alreay open */
  port = find_serial_port_by_path(app->serial_ports, path);
  if (port) {
    PRINTERR("Serial path already open\n");
    return 0;
  }
  fd = serial_open(path, opts);
  if (fd < 0) {
    return 0;
  }
  port = malloc(sizeof(struct SerialPort));
  port->app = app;
  
  if (!port) {
    PRINTERR("No memory for serial port\n");
    return 0;
  }
  
  port->poll = add_fd(app, fd, POLLIN, serial_recv, port);
  if (!port->poll) {
    PRINTERR("No more files allowed\n");
    free(port);
    return 0;
  }
  port->path = strdup(path);
  if (!port->path) {
    PRINTERR("No memory for path\n");
    free(port);
    return 0;
  }
  
  /* Link port */
  port->prevp = &app->serial_ports;
  port->next = app->serial_ports;
  if (port->next) {
    port->next->prevp = &port->next;
  }
  app->serial_ports = port;
  return 1;
}

static int
unix_serial_close(const char *path, void *context)
{
  struct SerialPort *port;
  struct AppContext *app = context;
  port = find_serial_port_by_path(app->serial_ports, path);
  if (!port) {
    PRINTERR("Trying to close unopened path: %s\n", path);
    return 0;
  }
  serial_port_destroy(port);
  return 1;
}
  
static int 
unix_serial_write(const char *path, const uint8_t *data, unsigned int len,
		  void *context)
{
  struct SerialPort *port;
  struct AppContext *app = context;
  port = find_serial_port_by_path(app->serial_ports, path);
  if (!port) {
    PRINTERR("Trying to send to unopened path: %s\n", path);
    return 0;
  }
  while(len > 0) {
    ssize_t written = write(port->poll->fd, data, len);
    if (written < 0) {
      PRINTERR("Failed to write to serial port: %s\n", strerror(errno));
      return 0;
    }
    data += written;
    len -= written;
  }
  return 1;
}

static int
handle_stdin(struct pollfd *poll, void *cb_data)
{
  uint8_t *buffer;
  int r;
  struct AppContext *app = cb_data;
  if (poll->revents == 0) return 0;
  r = native_message_get_input_buffer(&app->nm, &buffer);
  r = read(poll->fd, buffer, r);
  if (r == 0) return 0; /* EOF */
  if (r < 0) {
    PRINTERR("Failed to read stdin: %s\n", strerror(errno));
    return 0;
  }
  native_message_input(&app->nm, r);
  return 1;
}

static void 
write_stdout(const uint8_t *data, unsigned int len, void *context)
{
  write(STDOUT_FILENO, data, len);
}

static void 
message_handler(const uint8_t *msg, unsigned int len, void *context)
{
  struct AppContext *app = context;
  scratch_protocol_message_handler(&app->sp, msg, len);
}

static int exit_pending = 0;

static void
handle_sig(int s)
{
  exit_pending = 1;
}


static const struct NativeMessageCallbacks nm_callbacks =
  {
    message_handler,
    write_stdout
  };

static const struct ScratchSerialCallbacks serial_callbacks =
  {
    unix_serial_get_ports,
    unix_serial_open,
    unix_serial_close,
    unix_serial_write
  };
    

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

  native_message_init(&app.nm, &nm_callbacks, &app);
  scratch_protocol_init(&app.sp, &app.nm, &serial_callbacks, &app);
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
