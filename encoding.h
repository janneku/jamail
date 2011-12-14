#ifndef _ENCODING_H
#define _ENCODING_H

#include <stdexcept>
#include <stdint.h>
#include <streambuf>
#include <istream>
#include <ostream>

/* An unicode string (UTF-32) */
typedef std::basic_string<uint32_t> ustring;

class conv_error: public std::runtime_error {
public:
	conv_error(const std::string &what) :
		std::runtime_error(what)
	{}
};

std::string encode(const ustring &in, const char *enc);
ustring decode(const std::string &in, const char *enc);

class dec_streambuf: public std::basic_streambuf<uint32_t> {
public:
	dec_streambuf(std::istream &source, const std::string &enc);

private:
	std::istream &m_source;
	std::string m_enc;
	char m_readbuf[4096];
	char *m_input;
	size_t m_left;
	uint32_t m_buf[256];

	int_type underflow();
	void fillbuf();
};

class enc_streambuf: public std::basic_streambuf<uint32_t> {
public:
	enc_streambuf(std::ostream &sink, const std::string &enc);
	~enc_streambuf();

private:
	std::ostream &m_sink;
	std::string m_enc;
	uint32_t m_buf[1024];

	int_type overflow(int_type c = traits_type::eof());
};

#endif
