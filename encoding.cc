/*
 * jamail - Just another mail client
 *
 * Copyright 2011 Janne Kulmala <janne.t.kulmala@iki.fi>
 *
 * Program code is licensed with GNU LGPL 2.1. See COPYING.LGPL file.
 */
#include "encoding.h"
#include <assert.h>
#include <iconv.h>
#include <errno.h>

std::string encode(const ustring &in, const char *enc)
{
	char *in_ptr = (char *) &in[0];
	size_t in_left = in.size() * sizeof(in[0]);

	/*
	 * Strings are stored as sequences of UTF-32 characters stored in
	 * native byte order. Feed the string to iconv chunk at a time while
	 * allocating more room for the output. iconv will advance the input
	 * pointer for us.
	 */
	iconv_t conv = iconv_open(enc, "UTF-32");
	if (conv == iconv_t(-1)) {
		throw conv_error("Unable to initialize iconv");
	}

	std::string out;
	while (in_left > 0) {
		size_t pos = out.size();
		out.resize(pos + 256, 0);
		char *out_ptr = &out[pos];
		size_t out_left = 256;
		size_t ret =
			iconv(conv, &in_ptr, &in_left, &out_ptr, &out_left);
		if (ret == size_t(-1)) {
			if (errno == EILSEQ) {
				throw conv_error("Invalid char sequence");
			} else if (errno != E2BIG) {
				throw conv_error("Conversion failed");
			}
		}
		out.resize(pos + 256 - out_left);
	}
	iconv_close(conv);
	return out;
}

ustring decode(const std::string &in, const char *enc)
{
	char *in_ptr = (char *) &in[0];
	size_t in_left = in.size();

	/*
	 * iconv likes to write an UTF-32 BOM to the beginning of the output
	 * if the used byte order is not specified.
	 */
#if (BYTE_ORDER == LITTLE_ENDIAN)
	iconv_t conv = iconv_open("UTF-32LE", enc);
#else
	iconv_t conv = iconv_open("UTF-32BE", enc);
#endif
	if (conv == iconv_t(-1)) {
		throw conv_error("Unable to initialize iconv");
	}

	ustring out;
	while (in_left > 0) {
		size_t pos = out.size();
		out.resize(pos + 256, 0);
		char *out_ptr = (char *) &out[pos];
		size_t out_left = 256 * sizeof(uint32_t);
		size_t ret =
			iconv(conv, &in_ptr, &in_left, &out_ptr, &out_left);
		if (ret == size_t(-1)) {
			if (errno == EILSEQ) {
				throw conv_error("Invalid char sequence");
			} else if (errno != E2BIG) {
				throw conv_error("Conversion failed");
			}
		}
		out.resize(pos + 256 - out_left / sizeof(uint32_t));
	}
	iconv_close(conv);
	return out;
}
