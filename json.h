#ifndef _JSON_H
#define _JSON_H

#include <string>
#include <list>
#include <map>
#include <istream>
#include <stdexcept>
#include "common.h"

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

	long to_long() const;
	int to_int() const;
	double to_double() const;
	bool to_bool() const;
	ustring to_string() const;
	std::map<ustring, JSON_Value> children() const;
	std::list<JSON_Value> array() const;

	JSON_Value get(const ustring &key) const;
	JSON_Value get(const std::string &key) const;

	void insert(const ustring &key, const JSON_Value &value);
	void insert(const std::string &key, const JSON_Value &value);

	void push_back(const JSON_Value &value);

	JSON_Value(const JSON_Value &from);
	void operator =(const JSON_Value &from);

	void load(std::basic_istream<uint32_t> &is);

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
};

#endif
