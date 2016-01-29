#include "serial.h"
#include <string.h>
#include <errno.h>
#include <debug.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <limits.h>

static struct {
  unsigned int bit_rate;
  speed_t speed;
} speed_map[] = {
  {50, B50},
  {75, B75},
  {134, B134},
  {150, B150},
  {200, B200},
  {300, B300},
  {600, B600},
  {1200, B1200},
  {1800, B1800},
  {2400, B2400},
  {4800, B4800},
  {9600, B9600},
  {19200, B19200},
  {38400, B38400},
  {57600, B57600},
  {115200, B115200},
  {230400, B230400},
  {UINT_MAX, 0}
};
  
int
serial_open(const char *path, struct SerialOpts *opts)
{
  unsigned int s;
  int fd;
  struct termios tio;
  fd = open(path, O_RDWR);
  if (fd < 0) {
    PRINTERR("Failed to open serial port: %s\n",strerror(errno));
    return -1;
  }
  if (tcgetattr(fd, &tio)) {
    PRINTERR("Failed to get serial settings: %s\n",strerror(errno));
    return -1;
  }
  tio.c_iflag |= IGNBRK | IGNPAR | INPCK;
  tio.c_iflag &= ~(ISTRIP | INLCR | IGNCR | IXON | IXANY | IXOFF);

  tio.c_cflag &= ~(CSIZE | CSTOPB | PARENB | PARODD | CRTSCTS);
  tio.c_cflag |= CREAD;
  switch(opts->ctsFlowControl) {
  case 0:
    break;
  case 1:
    tio.c_iflag |= IXON | IXOFF;
    break;
  case 2:
    tio.c_cflag |= CRTSCTS;
    break;
  default:
    PRINTERR("Illegal flowcontrol value\n");
    return -1;
  }

  switch(opts->parityBit) {
  case 0:
    break;
  case 1:
    tio.c_cflag |= PARENB | PARODD;
    break;
  case 2:
    tio.c_cflag |= PARENB;
    break;
    PRINTERR("Illegal parity value\n");
    return -1;
  }

  switch(opts->dataBits) {
  case 5:
    tio.c_cflag |= CS5;
    break;
  case 6:
    tio.c_cflag |= CS5;
    break;
  case 7:
    tio.c_cflag |= CS5;
    break;
  case 8:
    tio.c_cflag |= CS5;
    break;
  default:
    PRINTERR("Illegal number of data bits\n");
    return -1;
  }
  
  switch(opts->stopBits) {
  case 0:
    break;
  case 1: /* Map 1.5 stop bits to 2 */
  case 2:
    tio.c_cflag |= CSTOPB;
    break;
  default:
    PRINTERR("Illegal stop bit value\n");
    return -1;
  }

  for (s = 0; speed_map[s].bit_rate < opts->bitRate; s++);
  if (speed_map[s].bit_rate != opts->bitRate) {
    PRINTERR("Illegal bit rate\n");
    return -1;
  }
  cfsetispeed(&tio, speed_map[s].speed);
  cfsetospeed(&tio, speed_map[s].speed);
  
  cfmakeraw(&tio);
  
  if (tcsetattr(fd, TCSAFLUSH, &tio)) {
    PRINTERR("Failed to set serial settings: %s\n",strerror(errno));
    return -1;
  }
  return fd;
}
