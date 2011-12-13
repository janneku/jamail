#include "utils.h"

ustring to_unicode(const std::string &in)
{
	ustring out(in.size(), 0);
	for (size_t i = 0; i < in.size(); ++i) {
		unsigned char c = in[i];
		if (c >= 128) {
			c = '?';
		}
		out[i] = c;
	}
	return out;
}
