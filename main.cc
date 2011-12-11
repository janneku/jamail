/*
 * jamail - Just another mail client
 *
 * Copyright 2011 Janne Kulmala <janne.t.kulmala@iki.fi>
 *
 * Program code is licensed with GNU LGPL 2.1. See COPYING.LGPL file.
 */
#include "utils.h"
#include <openssl/ssl.h>
#include <gtk/gtk.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <assert.h>
#include <fstream>
#include <sstream>
#include <list>
#include <stdexcept>

#define APP_NAME	"jamail"
#define UNUSED(x)	((void) (x))

class Account;

namespace {

const int PORT = 993;
const gint INVALID_GTK_WATCH = -1;

std::list<Account *> accounts;
bool debug_enabled = false;

GtkWidget *messages_view;
GtkWidget *text_view;

/* columns of the message list */
enum {
	COL_ID,
	COL_FROM,
	COL_SUBJECT,
	COL_ACCOUNT,
	MAX_COL
};

}

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
	std::string name;
	std::string email;
};

struct Envelope {
	std::string date;
	std::string subject;
	std::list<Header_Address> from;
	std::list<Header_Address> sender;
	std::list<Header_Address> reply_to;
	std::list<Header_Address> to;
	std::list<Header_Address> cc;
	std::list<Header_Address> bcc;
	std::string parent_id;
	std::string message_id;
};

class Account {
public:
	Account(const std::string &server, const std::string &user,
		const std::string &pw);
	~Account();

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
	gint m_watch;
	gint m_write_watch;
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

	DISABLE_COPY_AND_ASSIGN(Account);
};

class Main_Window {
public:
	Main_Window();
	~Main_Window();

private:
	GtkWidget *m_window;

	/* GTK callbacks */
	static void message_clicked(GtkTreeView *tree_view, GtkTreePath *path,
				    GtkTreeViewColumn *column, gpointer ptr);
};

namespace {

bool is_safe(int c)
{
	return (c >= ' ' && c <= 0x7f);
}

std::string hexdump(const std::string &s)
{
	std::string out;

	size_t i = 0;
	while (i < s.size()) {
		for (size_t j = 0; j < 16; ++j) {
			if (i + j < s.size())
				out += strf("%.2x ", s[i + j]);
			else
				out += "   ";
		}
		for (size_t j = 0; j < 16 && i + j < s.size(); ++j) {
			if (is_safe(s[i + j]))
				out += s[i + j];
			else
				out += '.';
		}
		i += 16;
		out += '\n';
	}
	return out;
}

int set_nonblock(int fd, bool enabled)
{
	int flags = fcntl(fd, F_GETFL) & ~O_NONBLOCK;
	if (enabled) {
		flags |= O_NONBLOCK;
	}
	return fcntl(fd, F_SETFL, flags);
}

/* check whether the next token is the given one */
bool check(std::istream &is, char token)
{
	/* skip leading space */
	char c = is.peek();
	while (is && isspace(c)) {
		is.get();
		c = is.peek();
	}
	return (is && c == token);
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
	if (!is || c != token) {
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
	if (!is || !is_atom(c)) {
		throw imap_parse_error("Expected an atom string");
	}
	std::string out;
	out += c;
	c = is.peek();
	while (is && is_atom(c)) {
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
		while (is && c != '\r') {
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
		if (!is) {
			throw imap_need_more();
		}

		/* skip the LF */
		c = is.get();
		if (!is || c != '\n') {
			throw imap_parse_error("Expected an LF");
		}

		out.resize(length, 0);
		if (!is.read(&out[0], length)) {
			throw imap_need_more();
		}

	} else if (skip(is, '"')) {
		/* a quoted string */
		char c = is.get();
		while (is && c != '"') {
			if (c == '\\') {
				/* Escaped char, used by Gmail's IMAP server */
				c = is.get();
				if (!is || !(c == '"' || c == '\\')) {
					throw std::runtime_error("Invalid escaped char");
				}
			}
			out += c;
			c = is.get();
		}
		if (!is) {
			throw imap_parse_error("Unterminated string");
		}
		return out;

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
			addr.name = parse_string(parser);
			parse_string(parser); /* ignored */
			std::string mailbox = parse_string(parser);
			std::string host = parse_string(parser);
			addr.email = mailbox + "@" + host;
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
	env.date = parse_string(parser);
	env.subject = parse_string(parser);
	env.from = parse_address_list(parser);
	env.sender = parse_address_list(parser);
	env.reply_to = parse_address_list(parser);
	env.to = parse_address_list(parser);
	env.cc = parse_address_list(parser);
	env.bcc = parse_address_list(parser);
	env.parent_id = parse_string(parser);
	env.message_id = parse_string(parser);
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

void load_config(const char *fname)
{
	std::ifstream f(fname);
	if (!f) {
		throw std::runtime_error(std::string("Can not open ") + fname);
	}

	std::string line;
	while (getline(f, line)) {
		if (line[0] == '#')
			continue;
		std::istringstream parser(line);

		std::string type;
		parser >> type;
		if (!parser)
			continue;

		if (type == "account") {
			std::string server, user, pw;
			parser >> server >> user >> pw;
			Account *acc = new Account(server, user, pw);
			accounts.push_back(acc);
		} else {
			printf("invalid config line: %s\n", type.c_str());
		}
	}
}

}

Account::Account(const std::string &server, const std::string &user,
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

Account::~Account()
{
	if (m_watch != INVALID_GTK_WATCH) {
		g_source_remove(m_watch);
	}
	remove_write_watch();
	if (m_conn != NULL) {
		SSL_free(m_conn);
	}
}

void Account::connect()
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

void Account::fetch_message(int id)
{
	send_command(strf("FETCH %d BODY[TEXT]", id));
	m_state = S_FETCH_BODY;
}

void Account::send_command(const std::string &cmd)
{
	m_send_buf += strf("%d ", m_next_cmd_id) + cmd + "\r\n";
	m_next_cmd_id++;
	try_write();
}

size_t Account::process_recv(const std::string &buf)
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
				} catch (const imap_parse_error &e) {
					printf("IMAP parse error: %s\n",
						e.what());
					printf("\"%s\"\n", line.c_str());
				}

				GtkTreeModel *model =
					gtk_tree_view_get_model(
						GTK_TREE_VIEW(messages_view));

				GtkTreeIter iter;
				gtk_list_store_append(GTK_LIST_STORE(model),
						      &iter);
				gtk_list_store_set(GTK_LIST_STORE(model), &iter,
					COL_ID, id,
					COL_FROM, env.from.front().email.c_str(),
					COL_SUBJECT, env.subject.c_str(),
					COL_ACCOUNT, this,
					-1);
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

				GtkTextBuffer *buf = gtk_text_view_get_buffer(
					GTK_TEXT_VIEW(text_view));
				gtk_text_buffer_set_text(buf, body.data(),
							 body.size());
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

void Account::ssl_handle_error(int ret)
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

void Account::install_write_watch()
{
	if (m_write_watch == INVALID_GTK_WATCH) {
		m_write_watch = g_io_add_watch(m_iochannel, G_IO_OUT,
					       write_ready, this);
	}
}

void Account::remove_write_watch()
{
	if (m_write_watch != INVALID_GTK_WATCH) {
		g_source_remove(m_write_watch);
		m_write_watch = INVALID_GTK_WATCH;
	}
}

void Account::try_read()
{
	size_t pos = m_recv_buf.size();
	m_recv_buf.resize(pos + 2048);
	int got = SSL_read(m_conn, &m_recv_buf[pos], 2048);
	if (got <= 0) {
		m_recv_buf.resize(pos);
		ssl_handle_error(got);
		return;
	}
	m_recv_buf.resize(pos + got);
	size_t i = process_recv(m_recv_buf);
	m_recv_buf.erase(m_recv_buf.begin(), m_recv_buf.begin() + i);
}

void Account::try_write()
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

int Account::data_available(GIOChannel *io, GIOCondition cond, gpointer ptr)
{
	UNUSED(io);
	UNUSED(cond);

	Account *self = (Account *) ptr;

	self->try_read();

	return TRUE;
}

int Account::write_ready(GIOChannel *io, GIOCondition cond, gpointer ptr)
{
	UNUSED(io);
	UNUSED(cond);

	Account *self = (Account *) ptr;

	self->try_write();

	return TRUE;
}

Main_Window::Main_Window()
{
	m_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(m_window), "jamail");
	gtk_window_set_default_size(GTK_WINDOW(m_window), 300, 300);
	gtk_container_set_border_width(GTK_CONTAINER(m_window), 4);
	g_signal_connect(G_OBJECT(m_window), "delete_event",
			 G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *vbox = gtk_vbox_new(FALSE, 4);

	GtkListStore *store =
		gtk_list_store_new(MAX_COL, G_TYPE_INT, G_TYPE_STRING,
				   G_TYPE_STRING, G_TYPE_POINTER);
	messages_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_signal_connect(G_OBJECT(messages_view), "row-activated",
			 G_CALLBACK(message_clicked), NULL);

	const struct {
		int id;
		const char *title;
	} columns[] = {
		{COL_ID, "ID"},
		{COL_FROM, "From"},
		{COL_SUBJECT, "Subject"},
		{-1, NULL}
	};

	for (int i = 0; columns[i].title; ++i) {
		GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
		GtkTreeViewColumn *column =
			gtk_tree_view_column_new_with_attributes(
				columns[i].title, renderer, "text",
				columns[i].id, NULL);
		gtk_tree_view_column_set_resizable(column, TRUE);
		gtk_tree_view_insert_column(GTK_TREE_VIEW(messages_view),
					    column, -1);
	}

	GtkWidget *scrollwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollwin),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scrollwin), messages_view);
	gtk_box_pack_start(GTK_BOX(vbox), scrollwin, TRUE, TRUE, 0);

	text_view = gtk_text_view_new();
	PangoFontDescription *font_desc =
		pango_font_description_from_string("monospace 10");
        gtk_widget_modify_font(text_view, font_desc);
        pango_font_description_free(font_desc);

	scrollwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollwin),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scrollwin), text_view);
	gtk_box_pack_start(GTK_BOX(vbox), scrollwin, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(m_window), vbox);

	gtk_widget_show_all(m_window);
}

Main_Window::~Main_Window()
{
	gtk_widget_destroy(m_window);
}

void Main_Window::message_clicked(GtkTreeView *tree_view, GtkTreePath *path,
				  GtkTreeViewColumn *column, gpointer ptr)
{
	UNUSED(column);

	Main_Window *self = (Main_Window *) ptr;

	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);

	GtkTreeIter iter;
	int id;
	Account *acc = NULL;
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, COL_ID, &id, COL_ACCOUNT, &acc, -1);

	acc->fetch_message(id);
}

int main(int argc, char **argv)
try {
	gtk_init(&argc, &argv);
	SSL_library_init();

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		std::string val;
		if (i < argc - 1) {
			val = argv[i + 1];
		}

		if (arg == "-d") {
			debug_enabled = true;
		}
	}

	std::string path = strf("%s/.jamail", getenv("HOME"));
	load_config(path.c_str());

	Main_Window mw;

	for (const_list_iter<Account *> i(accounts); i; i.next()) {
		Account *acc = *i;
		acc->connect();
	}

	gtk_main();
	return 0;

} catch (const std::exception &e) {
	fprintf(stderr, APP_NAME " ERROR: %s\n", e.what());
	return 1;
}

