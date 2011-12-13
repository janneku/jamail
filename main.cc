/*
 * jamail - Just another mail client
 *
 * Copyright 2011 Janne Kulmala <janne.t.kulmala@iki.fi>
 *
 * Program code is licensed with GNU LGPL 2.1. See COPYING.LGPL file.
 */
#include "utils.h"
#include "imap.h"
#include "json.h"
#include "encoding.h"
#include "common.h"
#include "imap.h"
#include <dirent.h>
#include <gtk/gtk.h>
#include <assert.h>
#include <sys/stat.h>
#include <fstream>
#include <sstream>

bool debug_enabled = false;

namespace {

std::string cache_path;

std::list<IMAP *> accounts;

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
			IMAP *acc = new IMAP(server, user, pw);
			accounts.push_back(acc);
		} else {
			printf("invalid config line: %s\n", type.c_str());
		}
	}
}

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
			 G_CALLBACK(message_clicked), this);

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
	UNUSED(self);

	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);

	GtkTreeIter iter;
	int id;
	IMAP *acc = NULL;
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, COL_ID, &id, COL_ACCOUNT, &acc, -1);

	acc->fetch_message(id);
}

std::list<Header_Address> parse_address_list(const JSON_Value &value)
{
	std::list<JSON_Value> array = value.array();
	std::list<Header_Address> out;
	for (const_list_iter<JSON_Value> i(array); i; i.next()) {
		Header_Address addr;
		addr.name = i->get("name").to_string();
		addr.email = i->get("email").to_string();
		out.push_back(addr);
	}
	return out;
}

JSON_Value json_address_list(const std::list<Header_Address> &list)
{
	JSON_Value out(JSON_Value::ARRAY);
	for (const_list_iter<Header_Address> i(list); i; i.next()) {
		JSON_Value addr(JSON_Value::OBJECT);
		addr.insert("name", JSON_Value(i->name));
		addr.insert("email", JSON_Value(i->email));
		out.push_back(addr);
	}
	return out;
}

void add_message_to_list(IMAP *account, const Envelope *env)
{
	GtkTreeModel *model =
		gtk_tree_view_get_model(GTK_TREE_VIEW(messages_view));

	/* GTK wants UTF-8 */
	std::string from = "?";
	if (!env->from.empty()) {
		encode(env->from.front().email, "UTF-8");
	}
	std::string subject = encode(env->subject, "UTF-8");

	GtkTreeIter iter;
	gtk_list_store_append(GTK_LIST_STORE(model),
			      &iter);
	gtk_list_store_set(GTK_LIST_STORE(model), &iter,
		COL_ID, env->id,
		COL_FROM, from.c_str(),
		COL_SUBJECT, subject.c_str(),
		COL_ACCOUNT, account,
		-1);
}

void add_message(IMAP *account, const Envelope *env)
{
	/* Serialize the headers of the message with JSON */
	JSON_Value message(JSON_Value::OBJECT);
	message.insert(to_unicode("sender"), json_address_list(env->sender));
	message.insert(to_unicode("from"), json_address_list(env->from));
	message.insert(to_unicode("to"), json_address_list(env->to));
	message.insert(to_unicode("cc"), json_address_list(env->cc));
	message.insert(to_unicode("bcc"), json_address_list(env->bcc));
	message.insert(to_unicode("reply_to"), json_address_list(env->reply_to));
	message.insert(to_unicode("subject"), env->subject);

	std::string buf = encode(message.serialize(), "UTF-8");
	std::string fname = cache_path + '/' + account->server() +
			    strf("/%d", env->id);
	std::ofstream f(fname.c_str(), std::ofstream::binary);
	f << buf;
	f.close();

	add_message_to_list(account, env);
}

void load_cache(IMAP *account)
{
	std::string path = cache_path + '/' + account->server();
	DIR *dir = opendir(path.c_str());
	if (dir == NULL)
		return;
	while (1) {
		dirent *de = readdir(dir);
		if (de == NULL)
			break;
		if (de->d_name[0] == '.')
			continue;

		std::string fname = path + '/' + de->d_name;
		std::ifstream f(fname.c_str(), std::ifstream::binary);
		if (!f) {
			debug("Can not open %s\n", fname.c_str());
			continue;
		}

		std::string buf;
		while (1) {
			size_t pos = buf.size();
			buf.resize(pos + 4096, 0);
			if (!f.read(&buf[pos], 4096)) {
				buf.resize(pos + f.gcount());
				break;
			}
		}

		std::basic_istringstream<uint32_t> parser(decode(buf, "UTF-8"));
		JSON_Value val;
		val.load(parser);

		Envelope env;
		env.subject = val.get("subject").to_string();
		env.sender = parse_address_list(val.get("sender"));
		env.from = parse_address_list(val.get("from"));
		env.to = parse_address_list(val.get("to"));
		env.cc = parse_address_list(val.get("cc"));
		env.bcc = parse_address_list(val.get("bcc"));
		env.reply_to = parse_address_list(val.get("reply_to"));

		add_message_to_list(account, &env);
	}
	closedir(dir);
}

void show_message(const std::string &body)
{
	GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
	gtk_text_buffer_set_text(buf, body.data(), body.size());
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

	cache_path = strf("%s/.cache/jamail", getenv("HOME"));
	mkdir(cache_path.c_str(), 0700);

	Main_Window mw;

	for (const_list_iter<IMAP *> i(accounts); i; i.next()) {
		IMAP *acc = *i;

		std::string path = cache_path + '/' + acc->server();
		mkdir(path.c_str(), 0700);

		load_cache(acc);
		acc->connect();
	}

	gtk_main();
	return 0;

} catch (const std::exception &e) {
	fprintf(stderr, APP_NAME " ERROR: %s\n", e.what());
	return 1;
}

