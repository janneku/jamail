#ifndef _ENCODING_H
#define _ENCODING_H

#include <stdexcept>
#include <stdint.h>

typedef std::basic_string<uint32_t> ustring;

class conv_error: public std::runtime_error {
public:
	conv_error(const std::string &what) :
		std::runtime_error(what)
	{}
};

std::string encode(const ustring &in, const char *enc);
ustring decode(const std::string &in, const char *enc);

#endif
