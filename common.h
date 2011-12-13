#ifndef _COMMON_H
#define _COMMON_H

#include <stdio.h>

#define APP_NAME	"jamail"
#define UNUSED(x)	((void) (x))

#define DISABLE_COPY_AND_ASSIGN(type)	\
	private: \
		type(const type &from); \
		void operator =(const type &from)

#define debug(...)	do { \
	if (debug_enabled) \
		printf("DEBUG: " __VA_ARGS__); \
	} while (0)

extern bool debug_enabled;

#endif
