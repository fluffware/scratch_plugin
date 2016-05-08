#ifndef __DEBUG_H__VP2LBB6GPL__
#define __DEBUG_H__VP2LBB6GPL__

#include <stdio.h>
#include <stdarg.h>

#define PRINTERR(...) fprintf(stderr,  __VA_ARGS__);fflush(stderr)
#define PRINTDEBUG(...) fprintf(stderr,  __VA_ARGS__);fflush(stderr)

#endif /* __DEBUG_H__VP2LBB6GPL__ */
