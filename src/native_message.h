#ifndef __NATIVE_MESSAGE_H__JH2TJOKQJ4__
#define __NATIVE_MESSAGE_H__JH2TJOKQJ4__

#include <stdint.h>

struct NativeMessage
{
  uint8_t *in_buffer; /* Current input buffer */
  uint8_t *alt_buffer; /* Alternate input buffer. Used for NUL termination 
			and unwrapping buffers. */
  unsigned int in_capacity;
  unsigned int in_len; /* Bytes received so far */
  unsigned int msg_left; /* Bytes left until messages ends or 0 if unknown */
  
  uint8_t *out_buffer;
  unsigned int out_capacity;
  unsigned int out_len;
  
  const struct NativeMessageCallbacks *callbacks;
  void *cb_context;
};

struct NativeMessageCallbacks
{
  void (*handle_message)(const uint8_t *msg, unsigned int len, void *context);
  void (*output_message)(const uint8_t *data, unsigned int len, void *context);
};

int
native_message_init(struct NativeMessage *nm, 
		    const struct NativeMessageCallbacks *callbacks,
		    void *cb_context);
void
native_message_destroy(struct NativeMessage *nm);


/* Get a buffer for writing input. Returns capacity */
unsigned int 
native_message_get_input_buffer(struct NativeMessage *nm, uint8_t **buffer);
		     
/* Handle input data written to input buffer */
void
native_message_input(struct NativeMessage *nm, 
		     unsigned int length);

/* Send a message */
void
native_message_send(struct NativeMessage *nm);

int
native_message_printf(struct NativeMessage *nm, const char *format, ...);

void
native_message_append_str(struct NativeMessage *nm, const char *str);

int
native_message_append_base64(struct NativeMessage *nm,
			     const uint8_t *data, unsigned int len);

#endif /* __NATIVE_MESSAGE_H__JH2TJOKQJ4__ */
