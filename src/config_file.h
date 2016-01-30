#ifndef __CONFIG_FILE_H__TR0ZK1X4QI__
#define __CONFIG_FILE_H__TR0ZK1X4QI__


struct ConfigData
{
  char **serial_ports;
};

void
config_data_destroy(struct ConfigData *cd);

struct ConfigData*
config_data_read(const char *file_name);

#endif /* __CONFIG_FILE_H__TR0ZK1X4QI__ */
