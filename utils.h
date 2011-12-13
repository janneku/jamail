/*
 * C++ utilities
 */
#ifndef _UTILS_H
#define _UTILS_H

#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <list>
#include <map>
#include <set>
#include <string>
#include <sstream>
#include <stdexcept>

extern inline std::string strf(const char *fmt, ...)
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

/*
 * WARNING: The iterator classes do not make a copy of the iterated container.
 * The user should make sure that the container does not get destroyed while
 * it is being iterated.
 */
template<class T>
class list_iter {
public:
	list_iter(std::list<T> &l) :
		i(l.begin()),
		m_end(l.end())
	{
	}
	T *operator -> () const { return &*i; }
	T &operator *() const { return *i; }
	operator bool() const { return i != m_end; }
	void next()
	{
		i++;
	}

private:
	typename std::list<T>::iterator i, m_end;
};

template<class T>
class safe_list_iter {
public:
	safe_list_iter(std::list<T> &l) :
		i(l.begin()),
		m_next(l.begin()),
		m_end(l.end())
	{
		if (m_next != m_end)
			m_next++;
	}
	typename std::list<T>::iterator iter() const { return i; }
	T *operator -> () const { return &*i; }
	T &operator *() const { return *i; }
	operator bool() const { return i != m_end; }
	void next()
	{
		i = m_next;
		if (m_next != m_end)
			m_next++;
	}

private:
	typename std::list<T>::iterator i, m_next, m_end;
};

template<class T>
class const_list_iter {
public:
	const_list_iter(const std::list<T> &l) :
		i(l.begin()),
		m_end(l.end())
	{
	}
	const T *operator -> () const { return &*i; }
	T operator *() const { return *i; }
	operator bool() const { return i != m_end; }
	void next()
	{
		i++;
	}

private:
	typename std::list<T>::const_iterator i, m_end;
};

template<class Key, class Value>
class map_iter {
public:
	map_iter(std::map<Key, Value> &l) :
		i(l.begin()),
		m_end(l.end())
	{
	}
	Key key() const { return i->first; }
	Value *operator -> () const { return &i->second; }
	Value &operator *() const { return i->second; }
	operator bool() const { return i != m_end; }
	void next()
	{
		i++;
	}

private:
	typename std::map<Key, Value>::iterator i, m_end;
};

template<class Key, class Value>
class safe_map_iter {
public:
	safe_map_iter(std::map<Key, Value> &l) :
		i(l.begin()),
		m_next(l.begin()),
		m_end(l.end())
	{
		if (m_next != m_end)
			m_next++;
	}
	Key key() const { return i->first; }
	typename std::map<Key, Value>::iterator iter() const { return i; }
	Value *operator -> () const { return &i->second; }
	Value &operator *() const { return i->second; }
	operator bool() const { return i != m_end; }
	void next()
	{
		i = m_next;
		if (m_next != m_end)
			m_next++;
	}

private:
	typename std::map<Key, Value>::iterator i, m_next, m_end;
};

template<class Key, class Value>
class const_map_iter {
public:
	const_map_iter(const std::map<Key, Value> &l) :
		i(l.begin()),
		m_end(l.end())
	{
	}
	Key key() const { return i->first; }
	const Value *operator -> () const { return &i->second; }
	Value operator *() const { return i->second; }
	operator bool() const { return i != m_end; }
	void next()
	{
		i++;
	}

private:
	typename std::map<Key, Value>::const_iterator i, m_end;
};

template<class T>
class set_iter {
public:
	set_iter(std::set<T> &l) :
		i(l.begin()),
		m_end(l.end())
	{
	}
	T *operator -> () const { return &*i; }
	T &operator *() const { return *i; }
	operator bool() const { return i != m_end; }
	void next()
	{
		i++;
	}

private:
	typename std::set<T>::iterator i, m_end;
};

template<class T>
class safe_set_iter {
public:
	safe_set_iter(std::set<T> &l) :
		i(l.begin()),
		m_next(l.begin()),
		m_end(l.end())
	{
		if (m_next != m_end)
			m_next++;
	}
	typename std::set<T>::iterator iter() const { return i; }
	T *operator -> () const { return &*i; }
	T &operator *() const { return *i; }
	operator bool() const { return i != m_end; }
	void next()
	{
		i = m_next;
		if (m_next != m_end)
			m_next++;
	}

private:
	typename std::set<T>::iterator i, m_next, m_end;
};

template<class T>
class const_set_iter {
public:
	const_set_iter(const std::set<T> &l) :
		i(l.begin()),
		m_end(l.end())
	{
	}
	const T *operator -> () const { return &*i; }
	T operator *() const { return *i; }
	operator bool() const { return i != m_end; }
	void next()
	{
		i++;
	}

private:
	typename std::set<T>::const_iterator i, m_end;
};

template<class Key, class Value>
void ins(std::multimap<Key, Value> &map, const Key &key, const Value &value)
{
	map.insert(std::pair<Key, Value>(key, value));
}

template<class Key, class Value>
void del(std::multimap<Key, Value> &map, const Key &key, const Value &value)
{
	typename std::multimap<Key, Value>::iterator i = map.lower_bound(key);
	while (i != map.end() && i->first == key) {
		if (i->second == value) {
			map.erase(i);
			return;
		}
		i++;
	}
	assert(0);
}

template<class Key, class Value>
void del_ifexists(std::multimap<Key, Value> &map, const Key &key,
		  const Value &value)
{
	typename std::multimap<Key, Value>::iterator i = map.lower_bound(key);
	while (i != map.end() && i->first == key) {
		if (i->second == value) {
			map.erase(i);
			break;
		}
		i++;
	}
}

template<class Key, class Value>
void ins(std::map<Key, Value> &map, const Key &key, const Value &value)
{
	map.insert(std::pair<Key, Value>(key, value));
}

template<class Key, class Value>
void del(std::map<Key, Value> &map, const Key &key)
{
	size_t num = map.erase(key);
	assert(num == 1);
}

template<class T>
void del(std::set<T> &set, const T &val)
{
	size_t num = set.erase(val);
	assert(num == 1);
}

template<class T>
void del_ifexists(std::set<T> &set, const T &val)
{
	typename std::set<T>::iterator i = set.find(val);
	if (i != set.end()) {
		set.erase(i);
	}
}

template<class Key, class Value>
Value get(const std::map<Key, Value> &map, const Key &key)
{
	typename std::map<Key, Value>::const_iterator i = map.find(key);
	assert(i != map.end());
	return i->second;
}

template<class Key, class Value>
Value &get(std::map<Key, Value> &map, const Key &key)
{
	typename std::map<Key, Value>::iterator i = map.find(key);
	assert(i != map.end());
	return i->second;
}

template<class Key, class Value>
Value getdefault(const std::map<Key, Value> &map, const Key &key,
		 const Value &def)
{
	typename std::map<Key, Value>::const_iterator i = map.find(key);
	if (i == map.end())
		return def;
	return i->second;
}

#endif
