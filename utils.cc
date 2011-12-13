#include "utils.h"

ustring to_unicode(const std::string &s)
{
	ustring out;
	for (size_t i = 0; i < s.size(); ++i) {
		unsigned char c = s[i];
		if (c < 128) {
			out += c;
		} else {
			out += '?';
		}
	}
	return out;
}
