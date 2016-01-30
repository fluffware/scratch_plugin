#include "config_file.h"
#include <stdio.h>
#include <json_parse.h>
#include <debug.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

void
config_data_destroy(struct ConfigData *cd)
{
  if (cd->serial_ports) {
    char **ports = cd->serial_ports;
    while(*ports) {
      free(*ports);
      ports++;
    }
    free(cd->serial_ports);
  }
  free(cd);
}
struct SerialPorts
{
  char **serial_ports;
  unsigned int n_ports;
};

static int 
serial_port_cb(const uint8_t **pp, void *cb_data)
{
  uint8_t dev[100];
  char * devp;
  char **ports_buf;
  struct SerialPorts *ports = cb_data;
  if (!json_parse_string_buffer(pp, dev, sizeof(dev))) {
    PRINTERR("Failed to parse serial port name\n");
    return 0;
  }
  devp = strdup((char*)dev);
  if (!devp) {
     PRINTERR("No memory for device name\n");
    return 0;
  }
  ports_buf = realloc(ports->serial_ports,(ports->n_ports + 2) * sizeof(char*));
  if (!ports_buf) {
    PRINTERR("Can't extend serial ports array\n");
    return 0;
  }
  
  ports_buf[ports->n_ports++] = devp;
  ports_buf[ports->n_ports] = NULL;
  ports->serial_ports = ports_buf;
  return 1;
}

static int
conf_param_cb(const uint8_t **pp, const char *key, void *cb_data)
{
  struct ConfigData *cd = cb_data;
  struct SerialPorts ports;
  ports.serial_ports = cd->serial_ports;
  ports.n_ports = 0;
  if (strcmp(key, "serial_ports") == 0) {
    int res = json_iterate_array(pp, serial_port_cb, &ports);
    cd->serial_ports = ports.serial_ports;
    if (!res) {
      PRINTERR("Failed to parse serial port list\n");
      return 0;
    }
  } else {
    PRINTERR("Unknown parameter %s\n", key);
    return 0;
  }
  return 1;
}

struct ConfigData *
config_data_read(const char *file_name)
{
  int res;
  int fd;
  const uint8_t *p;
  struct ConfigData *cd;
  uint8_t *read_buffer;
  unsigned int read_capacity = 256;
  unsigned int read_len = 0;
  char key[20];

  fd = open(file_name, O_RDONLY);
  if (fd < 0) {
    PRINTERR("Failed to open file %s: %s", file_name, strerror(errno));
    return NULL;
  }
  
  read_buffer = malloc(read_capacity);

  while(1) {
    ssize_t r;
    if (read_capacity - read_len <= 1) {
      uint8_t *b;
      read_capacity *= 2;
      b = realloc(read_buffer, read_capacity);
      if (!b) {
	free(read_buffer);
	close(fd);
	PRINTERR("Not enough memory for read buffer\n");
	return NULL;
      }
      read_buffer = b;
    }
    r = read(fd, read_buffer + read_len, read_capacity - read_len - 1);
    if (r < 0) {
      PRINTERR("Failed to read from file %s: %s\n", file_name, strerror(errno));
      free(read_buffer);
      close(fd);
      return NULL;
    }
    if (r == 0) break;
    read_len += r;
  }
  read_buffer[read_len] = '\0';
  close(fd);

  cd = malloc(sizeof(struct ConfigData));
  if (!cd) {
    PRINTERR("No memory for config data\n");
    free(read_buffer);
    return NULL;
  }
  cd->serial_ports = NULL; 
  p = read_buffer;
  json_skip_white(&p);
  res = json_iterate_object(&p, key, sizeof(key), conf_param_cb, cd);
  free(read_buffer);
  if (!res) {
    config_data_destroy(cd);
    return NULL;
  }
  return cd;
}
