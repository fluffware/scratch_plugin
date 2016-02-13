SUBDIRS=

AM_CFLAGS = 
plugindir = @plugindir@
noinst_PROGRAMS = 
plugin_PROGRAMS = ScratchDeviceHost

ScratchDeviceHost_SOURCES = main_unix.c \
json_parse.c json_parse.h \
serial.h \
serial_unix.c serial_unix.h \
config_file.c config_file.h \
native_message.c native_message.h \
scratch_protocol.c scratch_protocol.h \
debug.h

ScratchDeviceHost_LDADD=

plugin_DATA=$(top_srcdir)/plugin/edu.mit.scratch.device.json ScratchDeviceHost.json
