#include "scratch_protocol.h"
#include <serial.h>
#include <native_message.h>
#include <string.h>
#include <json_parse.h>
#include <debug.h>

static void 
version_handler(const uint8_t **pp, struct ScratchProtocol *sp)
{
  native_message_append_str(sp->nm,"[\"0.1\"]");
}

static void 
serial_list_handler(const uint8_t **pp, struct ScratchProtocol *sp)
{
  const char **port = sp->callbacks->serial_get_ports(sp->serial_context);
  native_message_append_str(sp->nm,"[");
  while(*port) {
    native_message_append_str(sp->nm,"\"");
    native_message_append_str(sp->nm,*port);
    native_message_append_str(sp->nm,"\"");
    port++;
    if (*port) {
      native_message_append_str(sp->nm,",");
    }
  }
  native_message_append_str(sp->nm,"]");
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



int
parse_path(const uint8_t **pp, struct ScratchProtocol *sp,
	   char *path, unsigned int len)
{
  if (!json_skip_comma(pp)) {
    PRINTERR("No comma before first parameter");
    native_message_append_str(sp->nm, "0");
    return 0;
  }
  if (!json_parse_string_buffer(pp, (uint8_t*)path, len)) {
    PRINTERR("Failed to parse path to serial device");
    native_message_append_str(sp->nm, "0");
    return 0;
  }
  return 1;
}

#define CMD_FAIL_RET  native_message_append_str(sp->nm, "0");return
static void 
serial_open_raw_handler(const uint8_t **pp, struct ScratchProtocol *sp)
{
  struct SerialOpts opts = default_serial_opts;
  char path[50];
  
  if (!parse_path(pp, sp, path, sizeof(path))) return;
  
  json_skip_white(pp);
  if (**pp == ',') {
    char key[20];
    (*pp)++;
    json_skip_white(pp);
    if (!json_iterate_object(pp,key, sizeof(key), serial_opts_cb, &opts)) {
      CMD_FAIL_RET;
    }
  }
  if (sp->callbacks->serial_open(path, &opts, sp->serial_context)) {
    native_message_append_str(sp->nm, "1");
  } else {
    native_message_append_str(sp->nm, "0");
  }
}

static void 
serial_close_handler(const uint8_t **pp, struct ScratchProtocol *sp)
{
  char path[50];
  
  if (!parse_path(pp, sp, path, sizeof(path))) return;

  if (sp->callbacks->serial_close(path, sp->serial_context)) {
    native_message_append_str(sp->nm, "1");
  } else {
    native_message_append_str(sp->nm, "0");
  }
}



struct WriterContext
{
  void *serial_ctxt;
  struct ScratchProtocol *sp;
  struct ScratchSerialCallbacks *callbacks;
  const char *path;
  uint16_t decode_buffer;
  uint16_t decode_shift;
};
  
static int
string_writer(const uint8_t *block, unsigned int len, void *cb_data)
{
  struct WriterContext *ctxt = cb_data;
  const struct ScratchSerialCallbacks *callbacks = ctxt->sp->callbacks;
  void *serial_ctxt = ctxt->sp->serial_context;
  const char *path = ctxt->path;
  
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
    ctxt->decode_buffer = ctxt->decode_buffer << 6 | v;
    ctxt->decode_shift += 6;
    if (ctxt->decode_shift >= 8) {
      ctxt->decode_shift -= 8;
      buf[buf_len++] = ctxt->decode_buffer >> ctxt->decode_shift;
      if (buf_len == sizeof(buf)) {
	if (!callbacks->serial_write(path, buf, buf_len, serial_ctxt))
	  return 0;
	buf_len = 0;
      }
    }
  }
  if (buf_len > 0) {
    if (!callbacks->serial_write(path, buf, buf_len, serial_ctxt))
      return 0;
  }
  return 1;
}

static void 
serial_send_raw_handler(const uint8_t **pp, struct ScratchProtocol *sp)
{
  struct WriterContext ctxt;
  char path[50];
  ctxt.sp = sp;
  
  if (!parse_path(pp, sp, path, sizeof(path))) return;
  if (!json_skip_comma(pp)) {
    PRINTERR("No comma after path\n");
    CMD_FAIL_RET;
  }
  ctxt.path = path;

  ctxt.decode_shift = 0;
  if (!json_parse_string(pp, string_writer, &ctxt)) {
    PRINTERR("Failed to write string to serial port\n");
    CMD_FAIL_RET;
  }
  native_message_append_str(sp->nm, "1");
}

struct CommandMap
{
  const char *command;
  void (*handler)(const uint8_t **pp, struct ScratchProtocol *sp);
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
scratch_protocol_message_handler(struct ScratchProtocol *sp, 
				 const uint8_t *msg, unsigned int len)
{
  const uint8_t *p = msg;
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
      native_message_printf(sp->nm,"[\"@\",\"%s\",", token); 
      cmd->handler(&p, sp);
      native_message_append_str(sp->nm,"]");
      sp->nm->out_buffer[sp->nm->out_len] = '\0';
      PRINTDEBUG("Reply: %d '%s'\n", sp->nm->out_len-4, sp->nm->out_buffer+4);
      native_message_send(sp->nm);
      break;
    }
    cmd++;
  }
}

void
scratch_protocol_init(struct ScratchProtocol *sp, struct NativeMessage *nm,
		      const struct ScratchSerialCallbacks *callbacks, 
		      void *context)
{
  sp->nm = nm;
  sp->callbacks = callbacks;
  sp->serial_context = context;
}

void
scratch_protocol_destroy(struct ScratchProtocol *sp)
{
}
