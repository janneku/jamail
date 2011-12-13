/*
 * jamail - Just another mail client
 *
 * Copyright 2011 Janne Kulmala <janne.t.kulmala@iki.fi>
 *
 * Program code is licensed with GNU LGPL 2.1. See COPYING.LGPL file.
 */
#include "imap.h"
#include "ioutils.h"
#include "utils.h"
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

namespace {

const int PORT = 993;
const int INVALID_GTK_WATCH = -1;

/* check whether the next token is the given one */
bool check(std::istream &is, char token)
{
	/* skip leading space */
	char c = is.peek();
	while (!is.eof() && isspace(c)) {
		is.get();
		c = is.peek();
	}
	return (!is.eof() && c == token);
}

bool skip(std::istream &is, char token)
{
	if (check(is, token)) {
		is.get();
		return true;
	}
	return false;
}

void expect(std::istream &is, char token)
{
	/* skips leading space */
	char c;
	is >> c;
	if (is.eof() || c != token) {
		throw imap_parse_error(strf("Expected token %c", token));
	}
}

bool is_atom(int c)
{
	return !isspace(c) && c != '(' && c != ')' && c != '{' && c != '['
		&& c != ']';
}

/* read an IMAP string (consisting of atom characters) */
std::string parse_astring(std::istream &is)
{
	/* skips leading space */
	char c;
	is >> c;
	if (is.eof() || !is_atom(c)) {
		throw imap_parse_error("Expected an atom string");
	}
	std::string out;
	out += c;
	c = is.peek();
	while (!is.eof() && is_atom(c)) {
		out += c;
		is.get();
		c = is.peek();
	}
	return out;
}

/* read a quoted or a literal IMAP string */
std::string parse_string(std::istream &is)
{
	std::string out;

	if (skip(is, '{')) {
		/* a literal string, with length of the string as a prefix */
		size_t length;
		is >> length;
		if (!is) {
			throw imap_parse_error("Invalid literal length");
		}
		expect(is, '}');

		/* skip leading space (and the CRLF) */
		char c = is.get();
		while (!is.eof() && c != '\r') {
			if (!isspace(c)) {
				throw imap_parse_error("Junk before CRLF");
			}
			c = is.get();
		}
		/*
		 * One CRLF-terminated line is read at a time from the server.
		 * The content of the string is after a CRLF, and we need to
		 * signal that the caller needs to give us another line.
		 */
		if (is.eof()) {
			throw imap_need_more();
		}

		/* skip the LF */
		c = is.get();
		if (is.eof() || c != '\n') {
			throw imap_parse_error("Expected an LF");
		}

		out.resize(length, 0);
		if (!is.read(&out[0], length)) {
			throw imap_need_more();
		}

	} else if (skip(is, '"')) {
		/* a quoted string */
		char c = is.get();
		while (!is.eof() && c != '"') {
			if (c == '\\') {
				/* Escaped char, used by Gmail's IMAP server */
				c = is.get();
				if (is.eof() || !(c == '"' || c == '\\')) {
					throw imap_parse_error("Invalid escaped char");
				}
			}
			out += c;
			c = is.get();
		}
		if (is.eof()) {
			throw imap_parse_error("Unterminated string");
		}

	} else {
		/* handle NIL */
		std::string nil = parse_astring(is);
		if (nil != "NIL") {
			throw imap_parse_error("Not a string or a NIL");
		}
	}
	return out;
}

std::list<Header_Address> parse_address_list(std::istream &parser)
{
	std::list<Header_Address> addresses;
	if (skip(parser, '(')) {
		while (!skip(parser, ')')) {
			expect(parser, '(');
			Header_Address addr;
			addr.name = to_unicode(parse_string(parser));
			parse_string(parser); /* ignored */
			std::string mailbox = parse_string(parser);
			std::string host = parse_string(parser);
			addr.email = to_unicode(mailbox + "@" + host);
			addresses.push_back(addr);
			expect(parser, ')');
		}
	} else {
		/* handle NIL */
		std::string nil = parse_astring(parser);
		if (nil != "NIL") {
			throw imap_parse_error("Not an address list or a NIL");
		}
	}
	return addresses;
}

Envelope parse_envelope(std::istream &parser)
{
	Envelope env;
	expect(parser, '(');
	env.date = to_unicode(parse_string(parser));
	env.subject = to_unicode(parse_string(parser));
	env.from = parse_address_list(parser);
	env.sender = parse_address_list(parser);
	env.reply_to = parse_address_list(parser);
	env.to = parse_address_list(parser);
	env.cc = parse_address_list(parser);
	env.bcc = parse_address_list(parser);
	env.parent_id = to_unicode(parse_string(parser));
	env.message_id = to_unicode(parse_string(parser));
	expect(parser, ')');
	return env;
}

void parse_body_struct(std::istream &parser)
{
	expect(parser, '(');

	if (check(parser, '(')) {
		/* a sequence of nested body structures */
		while (check(parser, '(')) {
			parse_body_struct(parser);
		}
		std::string subtype = parse_string(parser);

	} else {
		std::string type = parse_string(parser);
		std::string subtype = parse_string(parser);

		/* parameter list */
		if (skip(parser, '(')) {
			while (!skip(parser, ')')) {
				std::string key = parse_string(parser);
				std::string val = parse_string(parser);
			}
		} else {
			/* handle NIL */
			std::string nil = parse_astring(parser);
			if (nil != "NIL") {
				throw imap_parse_error("Not a param list or a NIL");
			}
		}

		std::string id = parse_string(parser);
		std::string descr = parse_string(parser);
		std::string encoding = parse_string(parser);

		size_t size;
		parser >> size; /* ignored */
		if (!parser) {
			throw imap_parse_error("Invalid body part size");
		}

		if (type == "TEXT") {
			size_t nlines;
			parser >> nlines; /* ignored */
			if (!parser) {
				throw imap_parse_error("Invalid number of lines");
			}

		} else if (type == "MESSAGE" && subtype == "RFC822") {
			parse_envelope(parser); /* ignored */
			parse_body_struct(parser); /* ignored */

			size_t nlines;
			parser >> nlines; /* ignored */
			if (!parser) {
				throw imap_parse_error("Invalid number of lines");
			}
		}
	}
	expect(parser, ')');
}

std::string parse_body_reply(std::istream &parser)
{
	expect(parser, '(');

	std::string body = parse_astring(parser);
	if (body != "BODY") {
		throw imap_parse_error("Expected BODY");
	}

	expect(parser, '[');
	std::string text = parse_astring(parser);
	if (text != "TEXT") {
		throw imap_parse_error("Expected TEXT");
	}
	expect(parser, ']');

	return parse_string(parser);
}

Envelope parse_fetch_reply(std::istream &parser)
{
	Envelope envelope;
	expect(parser, '(');
	while (!skip(parser, ')')) {
		std::string type = parse_astring(parser);

		if (type == "INTERNALDATE") {
			std::string date = parse_string(parser);

		} else if (type == "RFC822.SIZE") {
			size_t size;
			parser >> size; /* ignored */
			if (!parser) {
				throw imap_parse_error("Invalid msg size");
			}

		} else if (type == "FLAGS") {
			expect(parser, '(');
			while (!skip(parser, ')')) {
				std::string flag = parse_astring(parser);
			}

		} else if (type == "ENVELOPE") {
			envelope = parse_envelope(parser);

		} else if (type == "BODY") {
			parse_body_struct(parser); /* ignored */

		} else {
			debug("unknown fetch field: %s\n", type.c_str());
		}
	}
	return envelope;
}

}

IMAP::IMAP(const std::string &server, const std::string &user,
	   const std::string &pw) :
	m_state(S_IDLE),
	m_server(server),
	m_user(user),
	m_pw(pw),
	m_conn(NULL),
	m_watch(INVALID_GTK_WATCH),
	m_write_watch(INVALID_GTK_WATCH),
	m_logged_in(false),
	m_next_cmd_id(1),
	m_next_reply_id(1)
{
}

IMAP::~IMAP()
{
	if (m_watch != INVALID_GTK_WATCH) {
		g_source_remove(m_watch);
	}
	remove_write_watch();
	if (m_conn != NULL) {
		SSL_free(m_conn);
	}
}

void IMAP::connect()
{
	hostent *hp = gethostbyname(m_server.c_str());
	if (hp == NULL) {
		throw std::runtime_error("Can not to resolve host " + m_server);
	}

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		throw std::runtime_error("Can not to create socket");
	}

	sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_addr = *(in_addr *) hp->h_addr_list[0];
	sin.sin_port = htons(PORT);

	set_nonblock(fd, true);

	if (::connect(fd, (sockaddr *) &sin, sizeof sin)) {
		if (errno != EINPROGRESS) {
			close(fd);
			throw std::runtime_error("Can not to connect to " +
						 m_server);
		}
	}

	m_iochannel = g_io_channel_unix_new(fd);
	m_watch = g_io_add_watch(m_iochannel, G_IO_IN, data_available, this);
	m_state = S_CONNECTING;

	SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());
	SSL_CTX_set_options(ctx, SSL_OP_ALL);
	SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE|
			 SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

	m_conn = SSL_new(ctx);
	if (m_conn == NULL) {
		throw std::runtime_error("Can not create SSL object");
	}
	SSL_set_fd(m_conn, fd);

	/* begin SSL handshake */
	int ret = SSL_connect(m_conn);
	if (ret < 0) {
		ssl_handle_error(ret);
	}
}

void IMAP::fetch_message(int id)
{
	send_command(strf("FETCH %d BODY[TEXT]", id));
	m_state = S_FETCH_BODY;
}

void IMAP::send_command(const std::string &cmd)
{
	m_send_buf += strf("%d ", m_next_cmd_id) + cmd + "\r\n";
	m_next_cmd_id++;
	try_write();
}

size_t IMAP::process_recv(const std::string &buf)
{
	size_t begin = 0;
	size_t i = 0, j;
	/* TODO: fix the indentation */
	while (1)
	try {
		j = buf.find("\r\n", i);
		if (j == std::string::npos)
			break;

		std::string line = buf.substr(begin, j - begin);
		/* debug("RECV: %s\n", line.c_str()); */

		std::istringstream parser(line);
		bool untagged = true;
		if (!skip(parser, '*')) {
			untagged = false;
			int id;
			parser >> id;
			if (!parser || id != m_next_reply_id) {
				throw imap_parse_error("Invalid reply ID");
			}
			m_next_reply_id++;
		}

		switch (m_state) {
		case S_IDLE:
			break;

		case S_CONNECTING:
			/* send credentials */
			send_command(strf("LOGIN %s %s",
				     m_user.c_str(), m_pw.c_str()));
			m_state = S_LOGIN;
			break;

		case S_LOGIN:
			if (!untagged) {
				std::string status;
				parser >> status;
				if (status != "OK") {
					throw std::runtime_error("Unable to log in");
				}
				debug("logged in\n");
				send_command("SELECT INBOX");
				m_state = S_SELECT;
			}
			break;

		case S_SELECT:
			if (!untagged) {
				std::string status;
				parser >> status;
				if (status != "OK") {
					throw std::runtime_error("Unable to select");
				}
				send_command("FETCH 1:* full");
				m_state = S_FETCH;
			}
			break;

		case S_FETCH:
			if (!untagged) {
				std::string status;
				parser >> status;
				if (status != "OK") {
					throw std::runtime_error("Unable to fetch");
				}
				m_state = S_IDLE;

			} else {
				int id;
				parser >> id;
				if (!parser) {
					throw imap_parse_error("Invalid message ID");
				}
				std::string status = parse_astring(parser);

				Envelope env;
				/* TODO: write a proper parser */
				try {
					env = parse_fetch_reply(parser);
					env.id = id;

					add_message(this, &env);
				} catch (const imap_parse_error &e) {
					printf("IMAP parse error: %s\n",
						e.what());
					printf("\"%s\"\n", line.c_str());
				}
			}
			break;

		case S_FETCH_BODY:
			if (!untagged) {
				std::string status;
				parser >> status;
				if (status != "OK") {
					throw std::runtime_error("Unable to fetch");
				}
				m_state = S_IDLE;
			} else {
				int id;
				parser >> id;
				if (!parser) {
					throw imap_parse_error("Invalid message ID");
				}
				std::string status = parse_astring(parser);

				std::string body = parse_body_reply(parser);

				show_message(body);
			}
			break;

		default:
			break;
		}

		i = j + 2;
		begin = i;
	} catch (const imap_need_more &) {
		i = j + 2;
	}
	return begin;
}

void IMAP::ssl_handle_error(int ret)
{
	int error = SSL_get_error(m_conn, ret);
	switch (error) {
	case SSL_ERROR_WANT_READ:
		/*
		 * We don't care if SSL wants to read, because we always do.
		 * SSL also uses this to signal that it no longer needs to send.
		 */
		if (m_send_buf.empty()) {
			remove_write_watch();
		}
		break;
	case SSL_ERROR_WANT_WRITE:
		/* SSL wants to send something */
		install_write_watch();
		break;
	case SSL_ERROR_SYSCALL:
		throw std::runtime_error(strf("SSL: syscall error (%s)",
					 strerror(errno)));
	default:
		throw std::runtime_error("SSL: an error occured");
	}
}

void IMAP::install_write_watch()
{
	if (m_write_watch == INVALID_GTK_WATCH) {
		m_write_watch = g_io_add_watch(m_iochannel, G_IO_OUT,
					       write_ready, this);
	}
}

void IMAP::remove_write_watch()
{
	if (m_write_watch != INVALID_GTK_WATCH) {
		g_source_remove(m_write_watch);
		m_write_watch = INVALID_GTK_WATCH;
	}
}

void IMAP::try_read()
{
	while (1) {
		size_t pos = m_recv_buf.size();
		m_recv_buf.resize(pos + 4096);
		int got = SSL_read(m_conn, &m_recv_buf[pos], 4096);
		if (got <= 0) {
			m_recv_buf.resize(pos);
			ssl_handle_error(got);
			return;
		}
		m_recv_buf.resize(pos + got);
		size_t i = process_recv(m_recv_buf);
		m_recv_buf.erase(m_recv_buf.begin(), m_recv_buf.begin() + i);
	}
}

void IMAP::try_write()
{
	int written = SSL_write(m_conn, m_send_buf.data(), m_send_buf.size());
	if (written <= 0) {
		ssl_handle_error(written);
		return;
	}
	m_send_buf.erase(m_send_buf.begin(), m_send_buf.begin() + written);

	/*
	 * Install a write watch if there is more data to send, as SSL might
	 * able to send more data when the socket becomes writable.
	  */
	if (!m_send_buf.empty()) {
		install_write_watch();
	} else {
		remove_write_watch();
	}
}

int IMAP::data_available(GIOChannel *io, GIOCondition cond, gpointer ptr)
{
	UNUSED(io);
	UNUSED(cond);

	IMAP *self = (IMAP *) ptr;

	self->try_read();

	return TRUE;
}

int IMAP::write_ready(GIOChannel *io, GIOCondition cond, gpointer ptr)
{
	UNUSED(io);
	UNUSED(cond);

	IMAP *self = (IMAP *) ptr;

	self->try_write();

	return TRUE;
}

