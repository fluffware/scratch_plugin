
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <Windows.h>
#include <native_message.h>
#include <scratch_protocol.h>
#include <debug.h>
#include <string.h>
#include <assert.h>



static void
print_sys_error(const char *prefix)
{
  TCHAR errmsg[80];
  FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,NULL,GetLastError(),LANG_USER_DEFAULT,errmsg, sizeof(errmsg)/sizeof(TCHAR), NULL);
  PRINTERR("%s: %s\n", prefix,errmsg);
}

#define MAX_PORT 20

#define EVENT_TERMINATE 0
#define EVENT_READ 1
#define EVENT_WRITE 2

struct AppContext
{
  struct NativeMessage nm;
  struct ScratchProtocol sp;
  HANDLE out;
  HANDLE out_mutex;
  char *ports[MAX_PORT +2];
  unsigned int n_ports;
  struct SerialPort *serial_ports;
#if 0
  struct ConfigData *config_data;
#endif
};
struct SerialPort
{
  struct SerialPort *next;
  struct SerialPort **prevp;
  struct AppContext *app;
  HANDLE handle;
  char *path;
  HANDLE thread;
  HANDLE events[3];
};


static void
terminate_read_thread(struct SerialPort *ser_port)
{
  DWORD ret;
  SetEvent(ser_port->events[EVENT_TERMINATE]);
  ret = WaitForSingleObject(ser_port->thread, 10000);
  switch(ret) {
  case WAIT_OBJECT_0:
    break;
  case WAIT_TIMEOUT:
    PRINTERR("Timeout when waiting for thread to exit.\n");
    break;
  case WAIT_FAILED:
    print_sys_error("Failed to create read thread");
    break;
  }
  CloseHandle(ser_port->thread);
}

static void
serial_port_destroy(struct SerialPort *port)  
{
  int i;
  PRINTDEBUG("Destroying port %s\n", port->path);
  if (port->thread != INVALID_HANDLE_VALUE) {
    terminate_read_thread(port);
  }
  if (port->handle != INVALID_HANDLE_VALUE) {
    CloseHandle(port->handle);
  }
  for (i = 0; i < sizeof(port->events) / sizeof(port->events[0]); i++) {
    CloseHandle(port->events[i]);
  }
  /* Unlink */
  if (port->next) {
    port->next->prevp = port->prevp;
  }
  *port->prevp = port->next;
  
  free(port->path);
  free(port);
}

static struct SerialPort *
serial_port_add(struct AppContext *app, const char *path)  
{
  int i;
  struct SerialPort *port = malloc(sizeof(struct SerialPort));
  assert(port);
  port->path = malloc(strlen(path) + 1);
  assert(port->path);
  strcpy(port->path, path);
  if (app->serial_ports) {
    app->serial_ports->prevp = &port->next;
  }
  port->next = app->serial_ports;
  app->serial_ports = port;
  port->prevp = &app->serial_ports;

  port->app = app;
  port->handle = INVALID_HANDLE_VALUE;
  port->thread = INVALID_HANDLE_VALUE;
  for (i = 0; i < sizeof(port->events) / sizeof(port->events[0]); i++) {
    port->events[i] = CreateEvent(NULL, TRUE, TRUE, NULL);
  }
  return port;
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

static void
app_init(struct AppContext *app)
{
  app->n_ports = 0;
  app->serial_ports = NULL;
  app->out_mutex = INVALID_HANDLE_VALUE;
}

static void
app_cleanup(struct AppContext *app)
{
  int p;
  for (p = 0; p < app->n_ports; p ++) {
    free(app->ports[p]);
  }
  while(app->serial_ports) {
    serial_port_destroy(app->serial_ports);
  }
  scratch_protocol_destroy(&app->sp);
  native_message_destroy(&app->nm);
  if (app->out_mutex != INVALID_HANDLE_VALUE) {
    CloseHandle(app->out_mutex);
  }
}
#if 0
static struct pollfd*
add_fd(struct AppContext *app, HANDLE *handle, 
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
#endif


int
find_ports(char **ports, int max_ports)
{
  int pno;
  int nfound = 0;
  for (pno = 1; pno < max_ports - 1; pno++) {
    HANDLE port;
    char port_name[20];
    snprintf(port_name, sizeof(port_name), "\\\\.\\COM%d",pno);
    port = CreateFile( port_name,
		       GENERIC_READ | GENERIC_WRITE,
		       0,
		       NULL,
		       OPEN_EXISTING,
		       FILE_ATTRIBUTE_NORMAL,
		       NULL);
    if (port != INVALID_HANDLE_VALUE) {
      char *name;
      size_t l;
      CloseHandle(port);
      l = strlen(port_name + 4);
      name = malloc(l + 1);
      memcpy(name, port_name + 4, l + 1);
      assert(name);
      ports[nfound++] = name;
    }
  }
  ports[nfound] = NULL;
  return nfound;
}

static void
turn_off_line_mode(HANDLE *handle)
{
  DWORD mode;
  if (GetConsoleMode(handle, &mode)) {
    mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    SetConsoleMode(handle,mode);
  }
}

static const char **
win_serial_get_ports(void *context)
{
  struct AppContext *app = context;
  return (const char**)app->ports;
}

static void 
write_stdout(const uint8_t *data, unsigned int len, void *context);

DWORD WINAPI 
handle_read(LPVOID lpParam ) 
{
  OVERLAPPED async;
  DWORD r;
  uint8_t buffer[8];
  struct SerialPort *serport = lpParam;
  struct AppContext *app = serport->app;
  async.hEvent = serport->events[EVENT_READ];
  while(TRUE) {
    if (!ReadFile(serport->handle, buffer, sizeof(buffer), &r, &async)) {
      if (GetLastError() != ERROR_IO_PENDING) {
	print_sys_error("Failed to read from serial port (sync)");
	return 1;
      } else {
	DWORD res;
	res = WaitForMultipleObjects(2, serport->events, FALSE, INFINITE);
	switch(res) {
	case WAIT_OBJECT_0 + EVENT_TERMINATE: /* Terminate */
	  return 0;
	case WAIT_OBJECT_0 + EVENT_READ: /* Read */
	  if (!GetOverlappedResult(serport->handle, &async, &r, FALSE)) {
	    print_sys_error("Failed to read from serial port (async)");
	    return 1;
	  }
	}
      }	
    }
    
    WaitForSingleObject(app->out_mutex, INFINITE);
    
    native_message_append_str(&app->nm,"[\"serialRecv\",\"");
    native_message_append_str(&app->nm,serport->path);
    native_message_append_str(&app->nm,"\",\"");
    native_message_append_base64(&app->nm, buffer, r);
    native_message_append_str(&app->nm,"\"]");
    native_message_send(&app->nm);
    
    ReleaseMutex(app->out_mutex);
   
    
    //PRINTDEBUG("Got %d bytes\n", r);
  }
}

static struct {
  unsigned int bit_rate;
  DWORD speed;
} speed_map[] = {
  {110, CBR_110},
  {300, CBR_300},
  {600, CBR_600},
  {1200, CBR_1200},
  {2400, CBR_2400},
  {4800, CBR_4800},
  {9600, CBR_9600},
  {14400, CBR_14400},
  {19200, CBR_19200},
  {38400, CBR_38400},
  {57600, CBR_57600},
  {115200, CBR_115200},
  {256000, CBR_256000},
  {UINT_MAX, 0}
};

static int
win_serial_open(const char *port, struct SerialOpts *opts,
		void *context)
{
  struct SerialPort *serport;
  struct AppContext *app = context;
  int s;
  DCB dcb;
  COMMTIMEOUTS timeouts;
  HANDLE h;
  HANDLE thread;
  char path[20];
  snprintf(path, sizeof(path), "\\\\.\\%s",port);
  h = CreateFile(path,
		 GENERIC_READ | GENERIC_WRITE,
		 0,
		 NULL,
		 OPEN_EXISTING,
		 FILE_FLAG_OVERLAPPED,
		 NULL);
  if (h == INVALID_HANDLE_VALUE) {
    print_sys_error("Failed to open serial port");
    CloseHandle(h);
    return 0;
  }

  if (!GetCommState(h, &dcb)) {
    print_sys_error("Failed to get comm state");
    CloseHandle(h);
    return 0;
  }

  /* Set serial parameters */
  for (s = 0; speed_map[s].bit_rate < opts->bitRate; s++);
  if (speed_map[s].bit_rate != opts->bitRate) {
    PRINTERR("Illegal bit rate\n");
    return 0;
  }
  dcb.BaudRate = speed_map[s].speed;

  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDsrSensitivity = FALSE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  dcb.fNull = FALSE;
  dcb.fRtsControl = RTS_CONTROL_DISABLE;
  
  switch(opts->ctsFlowControl) {
  case 0:
   
    
    break;
  case 1:
    dcb.fOutX = TRUE;
    dcb.fInX = TRUE;
    break;
  case 2:
    dcb.fOutxCtsFlow = FALSE;
    dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
    break;
  default:
    PRINTERR("Illegal flowcontrol value\n");
    return 0;
  }

  switch(opts->parityBit) {
  case 0:
    dcb.Parity = NOPARITY;
    break;
  case 1:
    dcb.Parity = ODDPARITY;
    break;
  case 2:
    dcb.Parity = EVENPARITY;
    break;
    PRINTERR("Illegal parity value\n");
    return 0;
  }

  dcb.ByteSize = opts->dataBits;

  switch(opts->stopBits) {
  case 0:
    dcb.StopBits = ONESTOPBIT;
    break;
  case 1:
    dcb.StopBits = ONE5STOPBITS;
    break;
  case 2:
    dcb.StopBits = TWOSTOPBITS;
    break;
  default:
    PRINTERR("Illegal stop bit value\n");
    return 0;
  }
  
  if (!SetCommState(h, &dcb)) {
    print_sys_error("Failed to set comm state");
    CloseHandle(h);
    return 0;
  }

  /* Set serial timeouts */
  timeouts.ReadIntervalTimeout = 10; /* 10 ms */
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.ReadTotalTimeoutConstant = 0;

  timeouts.WriteTotalTimeoutMultiplier = 2;
  timeouts.WriteTotalTimeoutConstant = 500;
  
  if (!SetCommTimeouts(h, &timeouts)) {
    print_sys_error("Failed to set comm timeouts");
    CloseHandle(h);
    return 0;
  }

  serport = serial_port_add(app, port);
  serport->handle = h;
  ResetEvent(serport->events[EVENT_TERMINATE]);
  thread = CreateThread(NULL, 0, handle_read, serport, 0, NULL);
  if (!thread) {
    print_sys_error("Failed to create read thread");
    serial_port_destroy(serport);
    return 0;
  }
  serport->thread = thread;
  return 1;
}

static int 
win_serial_close(const char *path, void *context)
{
  struct AppContext *app = context;
  struct SerialPort *port;
  port = find_serial_port_by_path(app->serial_ports, path);
  if (!port) return 0;
  serial_port_destroy(port);
  return 1;
}

static int
win_serial_write(const char *path, const uint8_t *data, unsigned int len, 
		      void *context)
{
  OVERLAPPED async;
  DWORD written;
  struct AppContext *app = context;
  struct SerialPort *serport;
  serport = find_serial_port_by_path(app->serial_ports, path);
  if (!serport) return 0;
  async.hEvent = serport->events[EVENT_WRITE];
  if (!WriteFile(serport->handle, data, len, &written, &async)) {
    if (GetLastError() != ERROR_IO_PENDING) {
      print_sys_error("Failed to write to serial port (sync)");
      return 0;
    } else {
      if (!GetOverlappedResult(serport->handle, &async, &written, TRUE)) {
	print_sys_error("Failed to write to serial port (async)");
	return 0;
      }
    }
    
  }
  
  if (written != len) {
    PRINTERR("Not all bytes written to serial port (wrote %d of %d)\n",
	     written, len);
    return 0;
  }
  return 1;
}


static const struct ScratchSerialCallbacks serial_callbacks =
  {
    win_serial_get_ports,
    win_serial_open,
    win_serial_close,
    win_serial_write
  };

static void 
message_handler(const uint8_t *msg, unsigned int len, void *context)
{
  struct AppContext *app = context;
  scratch_protocol_message_handler(&app->sp, msg, len);
}

static void 
write_stdout(const uint8_t *data, unsigned int len, void *context)
{
  DWORD written;
  struct AppContext *app = context;
#if 0
  {
    int i;
    PRINTDEBUG("Writing to stdout %d %p %p\n", len, app->out, data);
    for (i = 0; i < len; i++) {
      PRINTDEBUG(" %02x", data[i]);
    }
    PRINTDEBUG("\n");
  }
#endif
#if 1
  {
    int i;
    PRINTDEBUG("stdout %d '",
	       data[0] | (data[1] << 8) | (data[2]<<16) | (data[3]<<24));
    for (i = 4; i < len; i++) {
      PRINTDEBUG("%c", data[i]);
    }
    PRINTDEBUG("'\n");
  }
#endif
  if (!WriteFile(app->out, data, len, &written, NULL)) {
    print_sys_error("Failed to write to stdout");
  }

  /* PRINTDEBUG("Writing to stdout done\n"); */
}

static const struct NativeMessageCallbacks nm_callbacks =
  {
    message_handler,
    write_stdout
  };


int
main()
{
  HANDLE in;
  struct AppContext app;
  DWORD avail;
  app_init(&app);
  PRINTDEBUG("Started\n");
  in = GetStdHandle(STD_INPUT_HANDLE);
  app.out = GetStdHandle(STD_OUTPUT_HANDLE);
  PRINTDEBUG("out = %p\n", app.out);
  app.out_mutex = CreateMutex(NULL, FALSE, NULL);
  native_message_init(&app.nm, &nm_callbacks, &app);
  scratch_protocol_init(&app.sp, &app.nm, &serial_callbacks, &app);

  turn_off_line_mode(in);
  
  app.n_ports = find_ports(app.ports, MAX_PORT + 2);
  
  while(TRUE) {
    uint8_t *buffer;
    DWORD r;
    r = native_message_get_input_buffer(&app.nm, &buffer);
    if (!PeekNamedPipe(in, NULL, 0, NULL, &avail, NULL)) {
      if (GetLastError() == ERROR_BROKEN_PIPE) break;
      print_sys_error("Failed to get available input. "
		      "Make sure stdin is a pipe");
      app_cleanup(&app);
      return EXIT_FAILURE;
    }
    /* PRINTDEBUG("Avail: %d\n", avail); */
    if (r > avail) {
      if(avail == 0) r = 1;
      else r = avail;
    }
    if (!ReadFile(in, buffer, r, &r, NULL)) {
      if (GetLastError() == ERROR_BROKEN_PIPE) break;
      print_sys_error("Failed to read from stdin");
    } else {
      if (r == 0) break;
      buffer[r] = '\0';
     
      /* PRINTDEBUG("Foo: '%s'\n", buffer+4); */
      WaitForSingleObject(app.out_mutex, INFINITE);
      native_message_input(&app.nm, r);
      ReleaseMutex(app.out_mutex);
      fflush(stderr);
      continue;
    }

   
  }
  PRINTDEBUG("Exiting\n");
  app_cleanup(&app);
  PRINTDEBUG("Cleanup done\n");
  return EXIT_SUCCESS;
}
