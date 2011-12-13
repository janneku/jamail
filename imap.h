#ifndef _IMAP_H
#define _IMAP_H

#include <openssl/ssl.h>
#include <stdexcept>
#include <gtk/gtk.h>
#include <list>

#include "common.h"

class imap_parse_error: public std::runtime_error {
public:
	imap_parse_error(const std::string &what) :
		std::runtime_error(what)
	{}
};

class imap_need_more: public std::exception {
public:
	imap_need_more() {}
};

struct Header_Address {
	ustring name;
	ustring email;
};

struct Envelope {
	int id;
	ustring date;
	ustring subject;
	std::list<Header_Address> from;
	std::list<Header_Address> sender;
	std::list<Header_Address> reply_to;
	std::list<Header_Address> to;
	std::list<Header_Address> cc;
	std::list<Header_Address> bcc;
	ustring parent_id;
	ustring message_id;
};

class IMAP {
public:
	IMAP(const std::string &server, const std::string &user,
	     const std::string &pw);
	~IMAP();

	std::string server() const { return m_server; }

	void connect();
	void fetch_message(int id);

private:
	enum {
		S_IDLE,
		S_CONNECTING,
		S_LOGIN,
		S_SELECT,
		S_FETCH,
		S_FETCH_BODY
	} m_state;

	std::string m_server;
	std::string m_user;
	std::string m_pw;

	SSL *m_conn;
	int m_watch;
	int m_write_watch;
	std::string m_send_buf;
	std::string m_recv_buf;
	GIOChannel *m_iochannel;
	bool m_logged_in;
	int m_next_cmd_id;
	int m_next_reply_id;

	void send_command(const std::string &cmd);
	size_t process_recv(const std::string &buf);
	void ssl_handle_error(int ret);
	void install_write_watch();
	void remove_write_watch();
	void try_read();
	void try_write();

	/* I/O watch callback */
	static int data_available(GIOChannel *io, GIOCondition cond,
				  gpointer ptr);
	static int write_ready(GIOChannel *io, GIOCondition cond, gpointer ptr);

	DISABLE_COPY_AND_ASSIGN(IMAP);
};

void add_message(IMAP *account, const Envelope *env);
void show_message(const std::string &body);

#endif
