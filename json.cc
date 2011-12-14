/*
 * A small JSON parser and serializer.
 *
 * Copyright 2011 Janne Kulmala <janne.t.kulmala@iki.fi>
 *
 * See LICENSE file for license.
 */
#include "json.h"
#include <stdarg.h>
#include <stdio.h>
#include <sstream>
#include <assert.h>

namespace {

/* should match the enum JSON_Value::Type */
const char *type_names[] = {
	NULL,
	"null",
	"string",
	"number (integer)",
	"number (floating point)",
	"boolean",
	"object",
	"array",
	NULL
};

/*
 * FAST conversion from ASCII to unicode. Invalid characters are replaced
 * with '?'.
 */
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

std::string strf(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	char buffer[1024];
	if (vsnprintf(buffer, sizeof buffer, fmt, args) >= int(sizeof buffer)) {
		throw std::runtime_error("strf() buffer overflow");
	}
	va_end(args);
	return buffer;
}

/* check whether the next token is the given one */
bool check(std::basic_istream<uint32_t> &is, char token)
{
	/* skip leading space */
	unsigned int c = is.peek();
	while (!is.eof() && isspace(c)) {
		is.get();
		c = is.peek();
	}
	return (!is.eof() && c == (unsigned int) token);
}

bool skip(std::basic_istream<uint32_t> &is, char token)
{
	if (check(is, token)) {
		is.get();
		return true;
	}
	return false;
}

void expect(std::basic_istream<uint32_t> &is, char token)
{
	if (!skip(is, token)) {
		throw json_parse_error(strf("Expected token %c", token));
	}
}

/* read a JSON word (a sequence of alphanumeric chars) */
std::string parse_word(std::basic_istream<uint32_t> &is)
{
	/* skip leading space */
	unsigned int c = is.peek();
	while (!is.eof() && isspace(c)) {
		is.get();
		c = is.peek();
	}
	if (is.eof() || !isalpha(c)) {
		throw json_parse_error("Expected a word");
	}
	std::string out;
	out += c;
	is.get();
	c = is.peek();
	while (!is.eof() && isalnum(c)) {
		out += c;
		is.get();
		c = is.peek();
	}
	return out;
}

/* read a JSON number */
std::string parse_number(std::basic_istream<uint32_t> &is)
{
	/* skip leading space */
	unsigned int c = is.peek();
	while (!is.eof() && isspace(c)) {
		is.get();
		c = is.peek();
	}
	if (is.eof() || !(isdigit(c) || c == '-')) {
		throw json_parse_error("Expected a number");
	}
	std::string out;
	if (c == '-') {
		out += '-';
		is.get();
		c = is.peek();
		if (is.eof() || !isdigit(c)) {
			throw json_parse_error("Expected a digit after -");
		}
	}
	while (!is.eof() && isdigit(c)) {
		out += c;
		is.get();
		c = is.peek();
	}

	/* decimal part */
	if (!is.eof() && c == '.') {
		out += '.';
		is.get();
		c = is.peek();
		while (!is.eof() && isdigit(c)) {
			out += c;
			is.get();
			c = is.peek();
		}
	}

	/* exponent */
	if (!is.eof() && (c == 'e' || c == 'E')) {
		out += 'e';
		is.get();
		c = is.peek();
		if (c == '-') {
			out += '-';
			is.get();
			c = is.peek();
			if (is.eof() || !isdigit(c)) {
				throw json_parse_error("Expected a digit after -");
			}
		}
		while (!is.eof() && isdigit(c)) {
			out += c;
			is.get();
			c = is.peek();
		}
	}
	return out;
}

/* read a quoted string (while converting escaped characters) */
ustring parse_string(std::basic_istream<uint32_t> &is)
{
	expect(is, '"');

	char table[128] = {};
	table['n'] = '\n';
	table['r'] = '\r';
	table['\"'] = '"';
	table['\\'] = '\\';

	ustring out;
	unsigned int c = is.get();
	while (!is.eof() && c != '"') {
		if (c == '\\') {
			/* An escaped character */
			c = is.get();
			if (is.eof()) {
				throw json_parse_error("Invalid escaped char");
			}
			if (c < 128 && table[c] != 0) {
				c = table[c];
			} else {
				/* 4-digit hex notation */
				ustring buf(4, 0);
				if (!is.read(&buf[0], 4)) {
					throw json_parse_error("Invalid escaped char");
				}
				/* TODO! */
				c = '?';
			}
		}
		out += c;
		c = is.get();
	}
	if (is.eof()) {
		throw json_parse_error("Unterminated string");
	}
	return out;
}

ustring escape(const ustring &in)
{
	char table[128] = {};
	table['\n'] = 'n';
	table['\r'] = 'r';
	table['\"'] = '"';
	table['\\'] = '\\';

	ustring out;
	size_t begin = 0;
	for (size_t i = 0; i < in.size(); ++i) {
		unsigned int c = in[i];
		/* Check whether the character needs to be escaped */
		if (c < 32 || (c < 128 && table[c] != 0)) {
			out.append(in.begin() + begin, in.begin() + i);
			out += '\\';
			if (c < 128 && table[c] != 0) {
				out += table[c];
			} else {
				out += to_unicode(strf("u%04x", c));
			}
			begin = i + 1;
		}
	}
	out.append(in.begin() + begin, in.end());
	return out;
}

}

JSON_Value::JSON_Value() :
	m_type(NULL_VALUE)
{
}

JSON_Value::JSON_Value(const ustring &s) :
	m_type(STRING)
{
	m_string = new ustring(s);
}

JSON_Value::JSON_Value(const std::string &s) :
	m_type(STRING)
{
	m_string = new ustring(to_unicode(s));
}

JSON_Value::JSON_Value(long value) :
	m_type(NUMBER_INT),
	m_int(value)
{
}

JSON_Value::JSON_Value(int value) :
	m_type(NUMBER_INT),
	m_int(value)
{
}

JSON_Value::JSON_Value(double value) :
	m_type(NUMBER_FLOAT),
	m_float(value)
{
}

JSON_Value::JSON_Value(bool value) :
	m_type(BOOLEAN),
	m_boolean(value)
{
}

JSON_Value::JSON_Value(Type type) :
	m_type(type)
{
	switch (m_type) {
	case NULL_VALUE:
		break;
	case OBJECT:
		m_children = new std::map<ustring, JSON_Value>;
		break;
	case ARRAY:
		m_array = new std::list<JSON_Value>;
		break;
	default:
		assert(0);
	}
}

JSON_Value::~JSON_Value()
{
	clear();
}

void JSON_Value::clear()
{
	switch (m_type) {
	case BOOLEAN:
	case NUMBER_INT:
	case NUMBER_FLOAT:
	case NULL_VALUE:
		/* do nothing */
		break;
	case STRING:
		delete m_string;
		break;
	case OBJECT:
		delete m_children;
		break;
	case ARRAY:
		delete m_array;
		break;
	default:
		assert(0);
	}
	m_type = NULL_VALUE;
}

long JSON_Value::to_long() const
{
	if (m_type == NUMBER_INT) {
		return m_int;
	} else if (m_type == NUMBER_FLOAT) {
		return m_float; /* round down! */
	} else {
		throw std::runtime_error(
			std::string("expected a number, but got JSON type ") +
			 type_names[m_type]);
	}
}

int JSON_Value::to_int() const
{
	if (m_type == NUMBER_INT) {
		if (int(m_int) != m_int) {
			throw std::runtime_error(
				strf("JSON number too large to fit to an integer: %ld",
				     m_int));
		}
		return m_int;
	} else if (m_type == NUMBER_FLOAT) {
		return m_float; /* round down! */
	} else {
		throw std::runtime_error(
			std::string("expected a number, but got JSON type ") +
			type_names[m_type]);
	}
}

double JSON_Value::to_double() const
{
	if (m_type == NUMBER_INT) {
		return m_int; /* may lose precision! */
	} else if (m_type == NUMBER_FLOAT) {
		return m_float;
	} else {
		throw std::runtime_error(
			std::string("expected a number, but got JSON type ") +
			type_names[m_type]);
	}
}

bool JSON_Value::to_bool() const
{
	if (m_type != BOOLEAN) {
		throw std::runtime_error(
			std::string("expected a boolean, but got JSON type ") +
			type_names[m_type]);
	}
	return m_boolean;
}

ustring JSON_Value::to_string() const
{
	if (m_type != STRING) {
		throw std::runtime_error(
			std::string("expected a string, but got JSON type ") +
			type_names[m_type]);
	}
	return *m_string;
}

std::map<ustring, JSON_Value> JSON_Value::children() const
{
	if (m_type != OBJECT) {
		throw std::runtime_error(
			std::string("expected an object, but got JSON type ") +
			type_names[m_type]);
	}
	return *m_children;
}

std::list<JSON_Value> JSON_Value::array() const
{
	if (m_type != ARRAY) {
		throw std::runtime_error(
			std::string("expected an array, but got JSON type ") +
			type_names[m_type]);
	}
	return *m_array;
}

JSON_Value JSON_Value::get(const ustring &key) const
{
	if (m_type != OBJECT) {
		throw std::runtime_error(
			std::string("expected an object, but got JSON type ") +
			type_names[m_type]);
	}
	std::map<ustring, JSON_Value>::const_iterator i
		= m_children->find(key);
	if (i == m_children->end())
		return JSON_Value();
	return i->second;
}

JSON_Value JSON_Value::get(const std::string &key) const
{
	if (m_type != OBJECT) {
		throw std::runtime_error(
			std::string("expected an object, but got JSON type ") +
			type_names[m_type]);
	}
	std::map<ustring, JSON_Value>::const_iterator i
		= m_children->find(to_unicode(key));
	if (i == m_children->end())
		return JSON_Value();
	return i->second;
}

void JSON_Value::insert(const ustring &key, const JSON_Value &value)
{
	if (m_type != OBJECT) {
		throw std::runtime_error(
			std::string("expected an object, but got JSON type ") +
			type_names[m_type]);
	}
	(*m_children)[key] = value;
}

void JSON_Value::insert(const std::string &key, const JSON_Value &value)
{
	if (m_type != OBJECT) {
		throw std::runtime_error(
			std::string("expected an object, but got JSON type ") +
			type_names[m_type]);
	}
	(*m_children)[to_unicode(key)] = value;
}

void JSON_Value::push_back(const JSON_Value &value)
{
	if (m_type != ARRAY) {
		throw std::runtime_error(
			std::string("expected an array, but got JSON type ") +
			type_names[m_type]);
	}
	m_array->push_back(value);
}

JSON_Value::JSON_Value(const JSON_Value &from) :
	m_type(NULL_VALUE)
{
	*this = from;
}

void JSON_Value::operator =(const JSON_Value &from)
{
	clear();

	m_type = from.m_type;
	switch (m_type) {
	case NULL_VALUE:
		/* do nothing */
		break;
	case BOOLEAN:
		m_boolean = from.m_boolean;
		break;
	case NUMBER_INT:
		m_int = from.m_int;
		break;
	case NUMBER_FLOAT:
		m_float = from.m_float;
		break;
	case STRING:
		m_string = new ustring(*from.m_string);
		break;
	case OBJECT:
		m_children =
		    new std::map<ustring, JSON_Value>(*from.m_children);
		break;
	case ARRAY:
		m_array = new std::list<JSON_Value>(*from.m_array);
		break;
	default:
		assert(0);
	}
}

JSON_Value::Type JSON_Value::equal_type() const
{
	if (m_type == NUMBER_INT)
		return NUMBER_FLOAT;
	return m_type;
}

bool JSON_Value::operator ==(const JSON_Value &other) const
{
	/*
	 * Check whether the values are of the same type. Integers and
	 * floats are considered the same.
	 */
	if (equal_type() != other.equal_type()) {
		return false;
	}

	switch (m_type) {
	case NULL_VALUE:
		/* Nulls are always equal */
		return true;

	case BOOLEAN:
		return m_boolean == other.m_boolean;
	case NUMBER_INT:
		if (other.m_type == NUMBER_INT) {
			return m_int == other.m_int;
		} else {
			return m_int == other.m_float;
		}
	case NUMBER_FLOAT:
		if (other.m_type == NUMBER_INT) {
			return m_float == other.m_int;
		} else {
			return m_float == other.m_float;
		}
	case STRING:
		return *m_string == *other.m_string;

	case OBJECT:
		if (m_children->size() != other.m_children->size())
			return false;

		/* Check that every key can be also found in the other map */
		for (std::map<ustring, JSON_Value>::const_iterator i =
		     m_children->begin(); i != m_children->end(); ++i) {
			std::map<ustring, JSON_Value>::const_iterator j =
				other.m_children->find(i->first);
			if (j == other.m_children->end())
				return false;
			if (i->second != j->second)
				return false;
		}
		/* No differences found */
		return true;

	case ARRAY: {
			if (m_array->size() != other.m_array->size())
				return false;
			/* Iterate over both arrays at the same time */
			std::list<JSON_Value>::const_iterator j =
				other.m_array->begin();
			for (std::list<JSON_Value>::const_iterator i =
			     m_array->begin(); i != m_array->end(); ++i) {
				if (*i != *j)
					return false;
				j++;
			}
			/* again, no differences */
			return true;
		}

	default:
		assert(0);
	}
}

bool JSON_Value::operator !=(const JSON_Value &other) const
{
	return !(*this == other);
}

void JSON_Value::load(std::basic_istream<uint32_t> &is)
{
	clear();

	/* skip leading space */
	unsigned int c = is.peek();
	while (!is.eof() && isspace(c)) {
		is.get();
		c = is.peek();
	}
	if (is.eof()) {
		throw json_parse_error("Expected a token");
	}
	switch (c) {
	case '"':
		m_string = new ustring(parse_string(is));
		m_type = STRING;
		break;

	case '{':
		/* An object (key-value pairs) */
		is.get();
		m_children = new std::map<ustring, JSON_Value>;
		m_type = OBJECT;
		while (!check(is, '}')) {
			ustring key = parse_string(is);
			expect(is, ':');
			JSON_Value val;
			val.load(is);
			(*m_children)[key] = val;
			if (!skip(is, ','))
				break;
		}
		expect(is, '}');
		break;

	case '[':
		/* An array */
		is.get();
		m_array = new std::list<JSON_Value>;
		m_type = ARRAY;
		while (!check(is, ']')) {
			JSON_Value val;
			val.load(is);
			m_array->push_back(val);
			if (!skip(is, ','))
				break;
		}
		expect(is, ']');
		break;

	default:
		if (isalpha(c)) {
			/* It is a word. The only possible words are below */
			std::string id = parse_word(is);
			if (id == "null") {
				m_type = NULL_VALUE;
			} else if (id == "true") {
				m_type = BOOLEAN;
				m_boolean = true;
			} else if (id == "false") {
				m_type = BOOLEAN;
				m_boolean = false;
			} else {
				throw json_parse_error("Unknown word");
			}
		} else if (isdigit(c) || c == '-') {
			/* A number (a float or an integer) */
			std::string num = parse_number(is);
			std::istringstream parser(num);

			if (num.find('.') != std::string::npos ||
			    num.find('e') != std::string::npos) {
				m_type = NUMBER_FLOAT;
				parser >> m_float;
			} else {
				m_type = NUMBER_INT;
				parser >> m_int;
			}
			if (!parser) {
				throw json_parse_error("Invalid number " + num);
			}
		} else {
			throw json_parse_error("Unknown character");
		}
		break;
	}
}

void JSON_Value::load_all(std::basic_istream<uint32_t> &is)
{
	load(is);

	char c = is.get();
	if (!is.eof()) {
		throw json_parse_error("Extra characters after JSON data");
	}
}

void JSON_Value::write(std::basic_ostream<uint32_t> &os, int indentation) const
{
	ustring out;
	bool first = true;

	switch (m_type) {
	case NULL_VALUE:
		os << to_unicode("null");
		break;
	case BOOLEAN:
		os << (m_boolean ? to_unicode("true") : to_unicode("false"));
		break;
	case NUMBER_INT:
		os << to_unicode(strf("%ld", m_int));
		break;
	case NUMBER_FLOAT:
		os << to_unicode(strf("%f", m_float));
		break;

	case STRING:
		os.put('"');
		os << escape(*m_string);
		os.put('"');
		break;

	case OBJECT:
		/*
		 * The children of an object are intended one level further
		 * from the indentation of the {} characters, one child
		 * for each line.
		 */
		os.put('{');
		os.put('\n');
		for (std::map<ustring, JSON_Value>::const_iterator i =
		     m_children->begin(); i != m_children->end(); ++i) {
			if (!first) {
				os.put(',');
				os.put('\n');
			}
			for (int j = 0; j <= indentation; ++j) {
				os.put('\t');
			}
			os.put('"');
			os << escape(i->first);
			os.put('"');
			os.put(':');
			os.put(' ');
			i->second.write(os, indentation + 1);
			first = false;
		}
		if (!first) {
			os.put('\n');
		}
		for (int j = 0; j < indentation; ++j) {
			os.put('\t');
		}
		os.put('}');
		break;

	case ARRAY:
		os.put('[');
		os.put('\n');
		for (std::list<JSON_Value>::const_iterator i =
		     m_array->begin(); i != m_array->end(); ++i) {
			if (!first) {
				os.put(',');
				os.put('\n');
			}
			for (int j = 0; j <= indentation; ++j) {
				os.put('\t');
			}
			i->write(os, indentation + 1);
			first = false;
		}
		if (!first) {
			os.put('\n');
		}
		for (int j = 0; j < indentation; ++j) {
			os.put('\t');
		}
		os.put(']');
		break;

	default:
		assert(0);
	}
}
