#ifndef __SERIAL_H__4M7RM0EJOZ__
#define __SERIAL_H__4M7RM0EJOZ__
#include <stdint.h>

struct SerialOpts
{
  unsigned int bitRate;
  unsigned int bufferSize;
  uint8_t ctsFlowControl;
  uint8_t dataBits;
  uint8_t parityBit;
  uint8_t stopBits;
};

int
serial_open(const char *path, struct SerialOpts *opts);

#endif /* __SERIAL_H__4M7RM0EJOZ__ */
