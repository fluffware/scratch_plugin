#include "native_message.h"
#include <debug.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

int
native_message_init(struct NativeMessage *nm, 
		    const struct NativeMessageCallbacks *callbacks,
		    void *cb_context)
{
  nm->out_buffer = NULL;
  nm->alt_buffer = NULL;
  nm->in_len = 0;
  nm->in_capacity = 1023;
  nm->in_buffer = malloc(nm->in_capacity + 1); /* Make room for NUL */
  if (!nm->in_buffer) {
    PRINTERR("No memory for receive buffer\n");
    return 0;
  }
  nm->alt_buffer = malloc(nm->in_capacity + 1); /* Make room for NUL */
  if (!nm->alt_buffer) {
    PRINTERR("No memory for receive buffer\n");
    return 0;
  }
  nm->msg_left = 0;

  nm->out_len = 4;
  nm->out_capacity = 1023;
  nm->out_buffer = malloc(nm->out_capacity + 1); /* Make room for NUL */
  if (!nm->out_buffer) {
    PRINTERR("No memory for send buffer\n");
    return 0;
  }
  nm->callbacks = callbacks;
  nm->cb_context = cb_context;
  return 1;
}

void
native_message_destroy(struct NativeMessage *nm)
{
  free(nm->in_buffer);
  free(nm->alt_buffer);
  free(nm->out_buffer);
}

unsigned int 
native_message_get_input_buffer(struct NativeMessage *nm, uint8_t **buffer)
{
  unsigned int pos = nm->in_len % nm->in_capacity; /* Wrap long messages */
  *buffer = &nm->in_buffer[pos];
  return nm->in_capacity - pos;
}

static void
swap_input_buffers(struct NativeMessage *nm)
{
  uint8_t *tmp = nm->in_buffer;
  nm->in_buffer = nm->alt_buffer;
  nm->alt_buffer = tmp;
}

void
native_message_input(struct NativeMessage *nm, unsigned int length)
{
  while(length > 0) {
    if (nm->msg_left == 0) {
      if (nm->in_len + length < 4) {
	nm->in_len += length;
	return;
      }
      length -= 4 - nm->in_len;
      nm->in_len = 4;
      nm->msg_left = *(uint32_t*)nm->in_buffer;
    } else {
      if (length >= nm->msg_left) {
	nm->in_len += nm->msg_left;
	length -= nm->msg_left;
	if (nm->in_len <= nm->in_capacity) {
	  if (length > 0) {
	    /* Move rest of buffer to alternate buffer to make room for NULL */
	    memcpy(nm->alt_buffer, nm->in_buffer+nm->in_len, length);
	  }
	  nm->in_buffer[nm->in_len] = '\0';
	  nm->callbacks->handle_message(nm->in_buffer + 4, nm->in_len - 4,
					nm->cb_context);
	  swap_input_buffers(nm);
	} else {
	  /* Overflow */
	  if (length > 0) {
	    /* Rotate buffer */
	    unsigned int start = nm->in_len % nm->in_capacity;
	    assert(nm->in_capacity - start >= length);
	    /* Copy remaining data to start */
	    memcpy(nm->alt_buffer, nm->in_buffer + start, length);
	    swap_input_buffers(nm);
	  }
	}
	nm->msg_left = 0;
	nm->in_len = 0;
      } else {
	nm->in_len += length;
	nm->msg_left -= length;
	return;
      }
    }
  }
    
}

int
native_message_printf(struct NativeMessage *nm, const char *format, ...)
{
  int w;
  va_list ap;
  va_start(ap, format);
  w = vsnprintf((char*)nm->out_buffer+nm->out_len, 
		nm->out_capacity - nm->out_len,
		format, ap);
  nm->out_len += w;
  if (nm->out_len > nm->out_capacity) {
    nm->out_len = nm->out_capacity;
  }
  va_end(ap);
  return w;
}

void
native_message_append_str(struct NativeMessage *nm, const char *str)
{
  size_t l = strlen(str);
  if (nm->out_len + l >= nm->out_capacity) {
    l = nm->out_capacity - nm->out_len - 1;
  }
  memcpy(nm->out_buffer + nm->out_len,str, l);
  nm->out_len += l;
}

static const uint8_t
base64chars[] = 
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int
native_message_append_base64(struct NativeMessage *nm,
			     const uint8_t *data, unsigned int len)
{
  uint32_t bits;
  unsigned int left = nm->out_capacity - nm->out_len;
  uint8_t *out = nm->out_buffer + nm->out_len;
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
  nm->out_len = out - nm->out_buffer;
  return 1;
}

void
native_message_send(struct NativeMessage *nm)
{
  *(uint32_t*)nm->out_buffer = nm->out_len - 4;
  nm->callbacks->output_message(nm->out_buffer, nm->out_len, nm->cb_context);
  nm->out_len = 4;
}
