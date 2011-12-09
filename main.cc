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

/* columns of the message list */
enum {
	COL_ID,
	COL_TITLE,
	MAX_COL
};

}

class Account {
public:
	Account(const std::string &server, const std::string &user,
		const std::string &pw);
	~Account();

	void connect();

private:
	enum {
		S_IDLE,
		S_CONNECTING,
		S_LOGIN,
		S_SELECT,
		S_FETCH
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

	size_t process_recv(const std::string &buf);
	void ssl_handle_error(int ret);
	void try_read();
	void try_write();

	/* I/O watch callback */
	static int data_available(GIOChannel *io, GIOCondition cond,
				  gpointer ptr);
	static int write_ready(GIOChannel *io, GIOCondition cond, gpointer ptr);
};

class MainWindow {
public:
	MainWindow();

private:
	GtkWidget *m_window;
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

void load_config(const char *fname)
{
	std::ifstream f(fname);
	if (!f) {
		throw std::runtime_error(std::string("Can not open ") + fname);
	}

	std::string line;
	while (getline(f, line)) {
		if (line.empty() || line[0] == '#')
			continue;
		std::istringstream parser(line);

		std::string type;
		parser >> type;
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
	m_logged_in(false)
{
}

Account::~Account()
{
	if (m_watch != INVALID_GTK_WATCH) {
		g_source_remove(m_watch);
	}
	if (m_write_watch != INVALID_GTK_WATCH) {
		g_source_remove(m_write_watch);
	}
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

size_t Account::process_recv(const std::string &buf)
{
	size_t i = 0;
	while (1) {
		size_t j = buf.find("\r\n", i);
		if (j == std::string::npos)
			break;

		std::string line = buf.substr(i, j - i);
		/* debug("RECV: %s\n", line.c_str()); */

		std::istringstream parser(line);
		std::string id;
		parser >> id;

		switch (m_state) {
		case S_IDLE:
			break;

		case S_CONNECTING:
			/* send credentials */
			m_send_buf += strf(". LOGIN %s %s\r\n",
					   m_user.c_str(), m_pw.c_str());
			try_write();
			m_state = S_LOGIN;
			break;

		case S_LOGIN:
			if (id == ".") {
				std::string status;
				parser >> status;
				if (status != "OK") {
					throw std::runtime_error("Unable to log in");
				}
				debug("logged in\n");
				m_send_buf += ". SELECT INBOX\r\n";
				try_write();
				m_state = S_SELECT;
			}
			break;

		case S_SELECT:
			if (id == ".") {
				std::string status;
				parser >> status;
				if (status != "OK") {
					throw std::runtime_error("Unable to select");
				}
				m_send_buf += ". FETCH 1:* full\r\n";
				try_write();
				m_state = S_FETCH;
			}
			break;

		case S_FETCH:
			if (id == ".") {
				std::string status;
				parser >> status;
				if (status != "OK") {
					throw std::runtime_error("Unable to fetch");
				}
				m_state = S_IDLE;

			} else if (id == "*") {
				std::string id, status, title;
				parser >> id >> status;
				title = line.substr(parser.tellg());

				GtkListStore *store =
					GTK_LIST_STORE(gtk_tree_view_get_model(
						GTK_TREE_VIEW(messages_view)));

				GtkTreeIter iter;
				gtk_list_store_append(store, &iter);
				gtk_list_store_set(store, &iter, COL_ID, id.c_str(),
						   COL_TITLE, title.c_str(), -1);
			}
			break;

		default:
			break;
		}

		i = j + 2;
	}
	return i;
}

void Account::ssl_handle_error(int ret)
{
	int error = SSL_get_error(m_conn, ret);
	switch (error) {
	case SSL_ERROR_WANT_READ:
		break;
	case SSL_ERROR_WANT_WRITE:
		if (m_write_watch == INVALID_GTK_WATCH) {
			debug("installing write watch\n");
			m_write_watch = g_io_add_watch(m_iochannel, G_IO_OUT,
						       write_ready, this);
		}
		break;
	default:
		throw std::runtime_error("SSL error occured");
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

	if (m_send_buf.empty() && m_write_watch != INVALID_GTK_WATCH) {
		debug("removing write watch\n");
		g_source_remove(m_write_watch);
		m_write_watch = INVALID_GTK_WATCH;
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

MainWindow::MainWindow()
{
	m_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(m_window), "jamail");
	gtk_container_set_border_width(GTK_CONTAINER(m_window), 4);
	g_signal_connect(G_OBJECT(m_window), "delete_event",
			 G_CALLBACK(gtk_main_quit), NULL);

	GtkListStore *store =
		gtk_list_store_new(MAX_COL, G_TYPE_STRING, G_TYPE_STRING);
	messages_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

	GtkCellRenderer *renderer1 = gtk_cell_renderer_text_new();
	GtkCellRenderer *renderer2 = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(messages_view),
		-1, "ID", renderer1, "text", COL_ID, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(messages_view),
		-1, "Title", renderer2, "text", COL_TITLE, NULL);

	GtkWidget *scrollwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollwin),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scrollwin), messages_view);

	gtk_container_add(GTK_CONTAINER(m_window), scrollwin);

	gtk_widget_show_all(m_window);
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

	MainWindow mw;

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

