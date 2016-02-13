#ifndef __SCRATCH_PROTOCOL_H__P954VNN5E4__
#define __SCRATCH_PROTOCOL_H__P954VNN5E4__

#include <serial.h>
#include <scratch_protocol.h>

struct ScratchProtocol
{
  struct NativeMessage *nm;
  const struct ScratchSerialCallbacks *callbacks;
  void *serial_context;
};

struct ScratchSerialCallbacks
{
  const char ** (*serial_get_ports)(void *context);
  int (*serial_open)(const char *port, struct SerialOpts *opts,
		     void *context);
  int (*serial_close)(const char *port, void *context);
  int (*serial_write)(const char *port, const uint8_t *data, unsigned int len, 
		      void *context);
};

void
scratch_protocol_init(struct ScratchProtocol *sp, struct NativeMessage *nm,
			const struct ScratchSerialCallbacks *callbacks,
			void *context);

void
scratch_protocol_destroy(struct ScratchProtocol *sp);

void
scratch_protocol_message_handler(struct ScratchProtocol *sp, 
				   const uint8_t *msg, unsigned int len);

#endif /* __SCRATCH_PROTOCOL_H__P954VNN5E4__ */
