/*
 * STL streams that perform unicode encoding and decoding (with iconv)
 *
 * Copyright 2011 Janne Kulmala <janne.t.kulmala@iki.fi>
 *
 * See LICENSE file for license.
 *
 */
#include "encoding.h"
#include <stdio.h>
#include <assert.h>
#include <iconv.h>
#include <sstream>
#include <string.h>
#include <errno.h>

std::string encode(const ustring &in, const char *enc)
{
	std::ostringstream sink;
	{
		enc_streambuf sb(sink, enc);
		std::basic_ostream<uint32_t> os(&sb);
		os << in;
	}
	return sink.str();
}

ustring decode(const std::string &in, const char *enc)
{
	std::istringstream source(in);
	dec_streambuf sb(source, enc);
	std::basic_istream<uint32_t> is(&sb);

	ustring out;
	while (1) {
		size_t pos = out.size();
		out.resize(pos + 256, 0);
		if (!is.read(&out[pos], 256)) {
			out.resize(pos + is.gcount());
			break;
		}
	}
	return out;
}

enc_streambuf::enc_streambuf(std::ostream &sink, const std::string &enc) :
	m_sink(sink), m_enc(enc)
{
	setp(m_buf, &m_buf[sizeof m_buf / sizeof(uint32_t)]);
}

enc_streambuf::~enc_streambuf()
{
	overflow();
}

enc_streambuf::int_type enc_streambuf::overflow(int_type c)
{
	iconv_t conv = iconv_open(m_enc.c_str(), "UTF-32");
	if (conv == iconv_t(-1)) {
		throw conv_error("Unable to initialize iconv");
	}

	char writebuf[1024];

	/* iconv will advance the input pointer for us */
	char *in_ptr = (char *) m_buf;
	size_t in_left = (char *) pptr() - in_ptr;
	while (in_left > 0) {
		char *out_ptr = writebuf;
		size_t out_left = sizeof writebuf;

		size_t ret =
			iconv(conv, &in_ptr, &in_left, &out_ptr, &out_left);
		if (ret == size_t(-1)) {
			if (errno == EILSEQ) {
				throw conv_error("Invalid char sequence");
			} else if (errno != E2BIG) {
				throw conv_error("Conversion failed");
			}
		}
		if (!m_sink.write(writebuf, sizeof writebuf - out_left)) {
			iconv_close(conv);
			return traits_type::eof();
		}
	}
	iconv_close(conv);
	m_buf[0] = c;
	setp(&m_buf[1], &m_buf[sizeof m_buf / sizeof(uint32_t)]);
	return 0;
}

dec_streambuf::dec_streambuf(std::istream &source, const std::string &enc) :
	m_source(source), m_enc(enc), m_input(NULL), m_left(0)
{
}

dec_streambuf::int_type dec_streambuf::underflow()
{
	fillbuf();
	if (m_left == 0) {
		return traits_type::eof();
	}

	/*
	 * iconv likes to write an UTF-32 BOM to the beginning of the output
	 * if the used byte order is not specified.
	 */
#if (BYTE_ORDER == LITTLE_ENDIAN)
	iconv_t conv = iconv_open("UTF-32LE", m_enc.c_str());
#else
	iconv_t conv = iconv_open("UTF-32BE", m_enc.c_str());
#endif
	if (conv == iconv_t(-1)) {
		throw conv_error("Unable to initialize iconv");
	}

	/* iconv will advance the input and output pointers for us */
	char *out_ptr = (char *) m_buf;
	size_t out_left = sizeof m_buf;
	while (out_left > 0 && m_left > 0) {
		size_t ret =
			iconv(conv, &m_input, &m_left, &out_ptr, &out_left);
		if (ret == size_t(-1)) {
			if (errno == EILSEQ) {
				throw conv_error("Invalid char sequence");
			} else if (errno != E2BIG) {
				throw conv_error("Conversion failed");
			}
		}
		fillbuf();
	}
	iconv_close(conv);
	setg(m_buf, m_buf, (uint32_t *) out_ptr);
	return m_buf[0];
}

void dec_streambuf::fillbuf()
{
	memmove(m_readbuf, m_input, m_left);
	m_input = m_readbuf;

	m_source.read(&m_readbuf[m_left], sizeof m_readbuf - m_left);
	m_left += m_source.gcount();
}
