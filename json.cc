/*
 * jamail - Just another mail client
 *
 * Copyright 2011 Janne Kulmala <janne.t.kulmala@iki.fi>
 *
 * Program code is licensed with GNU LGPL 2.1. See COPYING.LGPL file.
 */
#include "json.h"
#include "utils.h"
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

/* check whether the next token is the given one */
bool check(std::basic_istream<uint32_t> &is, char token)
{
	/* skip leading space */
	unsigned int c = is.peek();
	while (is && isspace(c)) {
		is.get();
		c = is.peek();
	}
	return (is && c == (unsigned int) token);
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
ustring parse_word(std::basic_istream<uint32_t> &is)
{
	/* skip leading space */
	unsigned int c = is.peek();
	while (is && isspace(c)) {
		is.get();
		c = is.peek();
	}
	if (!is || !isalpha(c)) {
		throw json_parse_error("Expected a word");
	}
	ustring out;
	out += c;
	c = is.peek();
	while (is && isalnum(c)) {
		out += c;
		is.get();
		c = is.peek();
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
	while (is && c != '"') {
		if (c == '\\') {
			/* An escaped character */
			c = is.get();
			if (!is) {
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
	if (!is) {
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

void JSON_Value::load(std::basic_istream<uint32_t> &is)
{
	clear();

	/* skip leading space */
	unsigned int c = is.peek();
	while (is && isspace(c)) {
		is.get();
		c = is.peek();
	}
	if (!is) {
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
			skip(is, ':');
			JSON_Value val;
			val.load(is);
			(*m_children)[key] = val;
			if (!skip(is, ','))
				break;
		}
		skip(is, '}');
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
		skip(is, ']');
		break;

	default:
		if (isalpha(c)) {
			/* It is a word. The only possible words are below */
			ustring id = parse_word(is);
			if (id == to_unicode("null")) {
				m_type = NULL_VALUE;
			} else if (id == to_unicode("true")) {
				m_type = BOOLEAN;
				m_boolean = true;
			} else if (id == to_unicode("false")) {
				m_type = BOOLEAN;
				m_boolean = false;
			} else {
				throw json_parse_error("Unknown word");
			}
		} else {
			/* TODO: Add support for floats */
			m_type = NUMBER_INT;
			is >> m_int;
			if (!is) {
				throw json_parse_error("Invalid token");
			}
		}
		break;
	}
}

ustring JSON_Value::serialize(int indentation) const
{
	ustring out;
	bool first = true;

	switch (m_type) {
	case NULL_VALUE:
		out = to_unicode("null");
		break;
	case BOOLEAN:
		out = m_boolean ? to_unicode("true") : to_unicode("false");
		break;
	case NUMBER_INT:
		out = to_unicode(strf("%ld", m_int));
		break;
	case NUMBER_FLOAT:
		out = to_unicode(strf("%f", m_float));
		break;

	case STRING:
		out = '"';
		out += escape(*m_string);
		out += '"';
		break;

	case OBJECT:
		/*
		 * The children of an object are intended one level further
		 * from the indentation of the {} characters, one child
		 * for each line.
		 */
		out = '{';
		out += '\n';
		for (const_map_iter<ustring, JSON_Value> i(*m_children); i;
		     i.next()) {
			if (!first) {
				out += ',';
				out += '\n';
			}
			for (int j = 0; j <= indentation; ++j) {
				out += '\t';
			}
			out += '"';
			out += escape(i.key());
			out += '"';
			out += ':';
			out += ' ';
			out += i->serialize(indentation + 1);
			first = false;
		}
		if (!first) {
			out += '\n';
		}
		for (int j = 0; j < indentation; ++j) {
			out += '\t';
		}
		out += '}';
		break;

	case ARRAY:
		out = '[';
		out += '\n';
		for (const_list_iter<JSON_Value> i(*m_array); i; i.next()) {
			if (!first) {
				out += ',';
				out += '\n';
			}
			for (int j = 0; j <= indentation; ++j) {
				out += '\t';
			}
			out += i->serialize(indentation + 1);
			first = false;
		}
		if (!first) {
			out += '\n';
		}
		for (int j = 0; j < indentation; ++j) {
			out += '\t';
		}
		out += ']';
		break;

	default:
		assert(0);
	}
	return out;
}
