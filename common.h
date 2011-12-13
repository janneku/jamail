#ifndef _COMMON_H
#define _COMMON_H

#include <stdio.h>
#include <string>
#include <stdint.h>

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

/* An unicode string (UTF-32) */
typedef std::basic_string<uint32_t> ustring;

#endif
