/*
 * A small JSON parser and serializer.
 *
 * Copyright 2011 Janne Kulmala <janne.t.kulmala@iki.fi>
 *
 * See LICENSE file for license.
 */
#ifndef _JSON_H
#define _JSON_H

#include <stdint.h>
#include <string>
#include <list>
#include <map>
#include <istream>
#include <stdexcept>

/* An unicode string (UTF-32) */
typedef std::basic_string<uint32_t> ustring;

class json_parse_error: public std::runtime_error {
public:
	json_parse_error(const std::string &what) :
		std::runtime_error(what)
	{}
};

class JSON_Value {
public:
	enum Type {
		NULL_VALUE = 1,
		STRING,
		NUMBER_INT,
		NUMBER_FLOAT,
		BOOLEAN,
		OBJECT,
		ARRAY,

		MAX_TYPE
	};

	JSON_Value();
	JSON_Value(const ustring &s);
	JSON_Value(const std::string &s);
	JSON_Value(long value);
	JSON_Value(int value);
	JSON_Value(double value);
	JSON_Value(bool value);
	JSON_Value(Type type);
	~JSON_Value();

	Type type() const { return m_type; }

	/*
	 * Convert the value to different types. An exception is raised when
	 * the conversion fails. Note that the returned JSONValue objects
	 * should not be destroyed.
	 */
	long to_long() const;
	int to_int() const;
	double to_double() const;
	bool to_bool() const;
	ustring to_string() const;
	std::map<ustring, JSON_Value> children() const;
	std::list<JSON_Value> array() const;

	/*
	 * If the value is a JSON object, return the value that matches the
	 * given key. If the key does not exists, returns a NULL JSON value.
	 */
	JSON_Value get(const ustring &key) const;
	JSON_Value get(const std::string &key) const;

	/*
	 * If the value is a JSON object, associate the given value to the
	 * given key. If the key is already exists, the previous value
	 * is overwritten.
	 */
	void insert(const ustring &key, const JSON_Value &value);
	void insert(const std::string &key, const JSON_Value &value);

	/*
	 * If the value is a JSON array, append the given value to the end
	 * of the array.
	 */
	void push_back(const JSON_Value &value);

	JSON_Value(const JSON_Value &from);
	void operator =(const JSON_Value &from);

	bool operator ==(const JSON_Value &other) const;
	bool operator !=(const JSON_Value &other) const;

	/*
	 * Load the contents of a JSON input from the given input stream.
	 * The current value is discarded and the input is loaded recursively
	 * into the current value. An exception is raised if the input contains
	 * syntax errors.
	 */
	void load(std::basic_istream<uint32_t> &is);

	/*
	 * Same as the above, but verifies that there is no extra characters
	 * after the JSON encoded data.
	 */
	void load_all(std::basic_istream<uint32_t> &is);

	/*
	 * Serialize the JSON value into a unicode string. The value
	 * is written recursively to the output using proper indentation.
	 */
	ustring serialize(int indentation = 0) const;

private:
	Type m_type;
	union {
		bool m_boolean;
		ustring *m_string;
		long m_int;
		double m_float;
		std::map<ustring, JSON_Value> *m_children;
		std::list<JSON_Value> *m_array;
	};

	void clear();
	Type equal_type() const;
};

#endif
