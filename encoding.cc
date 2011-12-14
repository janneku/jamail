/*
 * STL streams that perform unicode encoding and decoding (with iconv)
 *
 * Copyright 2011 Janne Kulmala <janne.t.kulmala@iki.fi>
 *
 * See LICENSE file for license.
 *
 */
#include "encoding.h"
#include <assert.h>
#include <iconv.h>
#include <sstream>
#include <string.h>
#include <errno.h>

std::string encode(const ustring &in, const char *enc)
{
	std::basic_istringstream<uint32_t> source(in);
	enc_streambuf sb(source, enc);
	std::istream is(&sb);

	std::string out;
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

enc_streambuf::enc_streambuf(std::basic_istream<uint32_t> &source,
			     const std::string &enc) :
	m_source(source), m_enc(enc), m_input(NULL), m_left(0)
{
}

enc_streambuf::int_type enc_streambuf::underflow()
{
	fillbuf();
	if (m_left == 0) {
		return traits_type::eof();
	}

	iconv_t conv = iconv_open(m_enc.c_str(), "UTF-32");
	if (conv == iconv_t(-1)) {
		throw conv_error("Unable to initialize iconv");
	}

	char *out_ptr = m_buf;
	size_t out_left = sizeof m_buf;

	size_t ret = iconv(conv, &m_input, &m_left, &out_ptr, &out_left);
	if (ret == size_t(-1)) {
		if (errno == EILSEQ) {
			throw conv_error("Invalid char sequence");
		} else if (errno != E2BIG) {
			throw conv_error("Conversion failed");
		}
	}
	iconv_close(conv);
	setg(m_buf, m_buf, out_ptr);
	return m_buf[0];
}

void enc_streambuf::fillbuf()
{
	memmove(m_readbuf, m_input, m_left);
	m_input = (char *) m_readbuf;

	/* Note, m_left is in bytes */
	m_source.read(&m_readbuf[m_left / sizeof(uint32_t)],
		      (sizeof m_readbuf - m_left) / sizeof(uint32_t));
	m_left += m_source.gcount() * sizeof(uint32_t);
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

	char *out_ptr = (char *) m_buf;
	size_t out_left = sizeof m_buf;

	size_t ret = iconv(conv, &m_input, &m_left, &out_ptr, &out_left);
	if (ret == size_t(-1)) {
		if (errno == EILSEQ) {
			throw conv_error("Invalid char sequence");
		} else if (errno != E2BIG) {
			throw conv_error("Conversion failed");
		}
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
