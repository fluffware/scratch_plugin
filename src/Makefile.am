SUBDIRS=

AM_CFLAGS = 
plugindir = @plugindir@
noinst_PROGRAMS = 
plugin_PROGRAMS = ScratchDeviceHost

ScratchDeviceHost_SOURCES = main.c \
json_parse.c json_parse.h \
serial.c serial.h \
config_file.c config_file.h \
debug.h

ScratchDeviceHost_LDADD=

plugin_DATA=$(top_srcdir)/plugin/edu.mit.scratch.device.json ScratchDeviceHost.json
