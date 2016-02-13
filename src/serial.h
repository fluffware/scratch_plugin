#ifndef __SERIAL_H__MBUJQ8UMMF__
#define __SERIAL_H__MBUJQ8UMMF__

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

#endif /* __SERIAL_H__MBUJQ8UMMF__ */
