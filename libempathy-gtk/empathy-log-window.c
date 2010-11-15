/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006-2007 Imendio AB
 * Copyright (C) 2007-2008 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 *
 * Authors: Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-logger/log-manager.h>

#include <libempathy/empathy-chatroom-manager.h>
#include <libempathy/empathy-chatroom.h>
#include <libempathy/empathy-message.h>
#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-time.h>

#include "empathy-log-window.h"
#include "empathy-account-chooser.h"
#include "empathy-chat-view.h"
#include "empathy-theme-manager.h"
#include "empathy-ui-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

typedef struct {
	GtkWidget         *window;

	GtkWidget         *notebook;

	GtkWidget         *entry_find;
	GtkWidget         *button_find;
	GtkWidget         *treeview_find;
	GtkWidget         *scrolledwindow_find;
	EmpathyChatView    *chatview_find;
	GtkWidget         *button_previous;
	GtkWidget         *button_next;

	GtkWidget         *vbox_chats;
	GtkWidget         *account_chooser_chats;
	GtkWidget         *entry_chats;
	GtkWidget         *calendar_chats;
	GtkWidget         *treeview_chats;
	GtkWidget         *scrolledwindow_chats;
	EmpathyChatView    *chatview_chats;

	gchar             *last_find;

	TplLogManager     *log_manager;

	/* Those are only used while waiting for the account chooser to be ready */
	TpAccount         *selected_account;
	gchar             *selected_chat_id;
	gboolean          selected_is_chatroom;
} EmpathyLogWindow;

static void     log_window_destroy_cb                      (GtkWidget        *widget,
							    EmpathyLogWindow *window);
static void     log_window_entry_find_changed_cb           (GtkWidget        *entry,
							    EmpathyLogWindow *window);
static void     log_window_find_changed_cb                 (GtkTreeSelection *selection,
							    EmpathyLogWindow *window);
static void     log_window_find_populate                   (EmpathyLogWindow *window,
							    const gchar      *search_criteria);
static void     log_window_find_setup                      (EmpathyLogWindow *window);
static void     log_window_button_find_clicked_cb          (GtkWidget        *widget,
							    EmpathyLogWindow *window);
static void     log_window_entry_find_activate_cb          (GtkWidget        *widget,
							    EmpathyLogWindow *window);
static void     log_window_button_next_clicked_cb          (GtkWidget        *widget,
							    EmpathyLogWindow *window);
static void     log_window_button_previous_clicked_cb      (GtkWidget        *widget,
							    EmpathyLogWindow *window);
static void     log_window_button_close_clicked_cb         (GtkWidget        *widget,
							    EmpathyLogWindow *window);
static void     log_window_chats_changed_cb                (GtkTreeSelection *selection,
							    EmpathyLogWindow *window);
static void     log_window_chats_populate                  (EmpathyLogWindow *window);
static void     log_window_chats_setup                     (EmpathyLogWindow *window);
static void     log_window_chats_accounts_changed_cb       (GtkWidget        *combobox,
							    EmpathyLogWindow *window);
static void     log_window_chats_set_selected              (EmpathyLogWindow *window);
static gboolean log_window_chats_get_selected              (EmpathyLogWindow *window,
							    TpAccount       **account,
							    gchar           **chat_id,
							    gboolean         *is_chatroom);
static void     log_window_chats_get_messages              (EmpathyLogWindow *window,
							    GDate      *date_to_show);
static void     log_window_calendar_chats_day_selected_cb  (GtkWidget        *calendar,
							    EmpathyLogWindow *window);
static void     log_window_calendar_chats_month_changed_cb (GtkWidget        *calendar,
							    EmpathyLogWindow *window);
static void     log_window_entry_chats_changed_cb          (GtkWidget        *entry,
							    EmpathyLogWindow *window);
static void     log_window_entry_chats_activate_cb         (GtkWidget        *entry,
							    EmpathyLogWindow *window);

enum {
	COL_FIND_ACCOUNT_ICON,
	COL_FIND_ACCOUNT_NAME,
	COL_FIND_ACCOUNT,
	COL_FIND_CHAT_NAME,
	COL_FIND_CHAT_ID,
	COL_FIND_IS_CHATROOM,
	COL_FIND_DATE,
	COL_FIND_DATE_READABLE,
	COL_FIND_COUNT
};

enum {
	COL_CHAT_ICON,
	COL_CHAT_NAME,
	COL_CHAT_ACCOUNT,
	COL_CHAT_ID,
	COL_CHAT_IS_CHATROOM,
	COL_CHAT_COUNT
};

static EmpathyLogWindow *log_window = NULL;

static void
account_manager_prepared_cb (GObject *source_object,
			     GAsyncResult *result,
			     gpointer user_data)
{
	TpAccountManager *account_manager = TP_ACCOUNT_MANAGER (source_object);
	EmpathyLogWindow *window = user_data;
	guint account_num;
	GList *accounts;
	GError *error = NULL;

	if (log_window == NULL)
		return;

	if (!tp_account_manager_prepare_finish (account_manager, result, &error)) {
		DEBUG ("Failed to prepare account manager: %s", error->message);
		g_error_free (error);
		return;
	}

	accounts = tp_account_manager_get_valid_accounts (account_manager);
	account_num = g_list_length (accounts);
	g_list_free (accounts);

	if (account_num > 1) {
		gtk_widget_show (window->vbox_chats);
		gtk_widget_show (window->account_chooser_chats);
	} else {
		gtk_widget_hide (window->vbox_chats);
		gtk_widget_hide (window->account_chooser_chats);
	}
}

static void
account_chooser_ready_cb (EmpathyAccountChooser *chooser,
			EmpathyLogWindow *window)
{
	gtk_notebook_set_current_page (GTK_NOTEBOOK (window->notebook), 1);

	/* We'll display the account once the model has been populate with the chats
	 * of this account. */
	empathy_account_chooser_set_account (EMPATHY_ACCOUNT_CHOOSER (
		window->account_chooser_chats), window->selected_account);
}

static void
select_account_once_ready (EmpathyLogWindow *self,
			   TpAccount *account,
			   const gchar *chat_id,
			   gboolean is_chatroom)
{
	EmpathyAccountChooser *account_chooser = EMPATHY_ACCOUNT_CHOOSER (self->account_chooser_chats);

	tp_clear_object (&self->selected_account);
	self->selected_account = g_object_ref (account);

	g_free (self->selected_chat_id);
	self->selected_chat_id = g_strdup (chat_id);

	self->selected_is_chatroom = is_chatroom;

	if (empathy_account_chooser_is_ready (account_chooser))
		account_chooser_ready_cb (account_chooser, self);
	else
		/* Chat will be selected once the account chooser is ready */
		g_signal_connect (account_chooser, "ready",
				  G_CALLBACK (account_chooser_ready_cb), self);
}

GtkWidget *
empathy_log_window_show (TpAccount  *account,
			const gchar *chat_id,
			gboolean     is_chatroom,
			GtkWindow   *parent)
{
	EmpathyAccountChooser   *account_chooser;
	TpAccountManager        *account_manager;
	GtkBuilder             *gui;
	gchar                  *filename;
	EmpathyLogWindow       *window;

	if (log_window != NULL) {
		gtk_window_present (GTK_WINDOW (log_window->window));

		if (account != NULL && chat_id != NULL) {
			gtk_notebook_set_current_page (GTK_NOTEBOOK (log_window->notebook), 1);
			select_account_once_ready (log_window, account, chat_id, is_chatroom);
		}

		return log_window->window;
	}

	log_window = g_new0 (EmpathyLogWindow, 1);
	log_window->log_manager = tpl_log_manager_dup_singleton ();

	window = log_window;

	filename = empathy_file_lookup ("empathy-log-window.ui",
					"libempathy-gtk");
	gui = empathy_builder_get_file (filename,
				       "log_window", &window->window,
				       "notebook", &window->notebook,
				       "entry_find", &window->entry_find,
				       "button_find", &window->button_find,
				       "treeview_find", &window->treeview_find,
				       "scrolledwindow_find", &window->scrolledwindow_find,
				       "button_previous", &window->button_previous,
				       "button_next", &window->button_next,
				       "entry_chats", &window->entry_chats,
				       "calendar_chats", &window->calendar_chats,
				       "vbox_chats", &window->vbox_chats,
				       "treeview_chats", &window->treeview_chats,
				       "scrolledwindow_chats", &window->scrolledwindow_chats,
				       NULL);
	g_free (filename);

	empathy_builder_connect (gui, window,
			      "log_window", "destroy", log_window_destroy_cb,
			      "entry_find", "changed", log_window_entry_find_changed_cb,
			      "entry_find", "activate", log_window_entry_find_activate_cb,
			      "button_previous", "clicked", log_window_button_previous_clicked_cb,
			      "button_next", "clicked", log_window_button_next_clicked_cb,
			      "button_close", "clicked", log_window_button_close_clicked_cb,
			      "button_close2", "clicked", log_window_button_close_clicked_cb,
			      "button_find", "clicked", log_window_button_find_clicked_cb,
			      "entry_chats", "changed", log_window_entry_chats_changed_cb,
			      "entry_chats", "activate", log_window_entry_chats_activate_cb,
			      NULL);

	g_object_unref (gui);

	g_object_add_weak_pointer (G_OBJECT (window->window),
				   (gpointer) &log_window);

	/* We set this up here so we can block it when needed. */
	g_signal_connect (window->calendar_chats, "day-selected",
			  G_CALLBACK (log_window_calendar_chats_day_selected_cb),
			  window);
	g_signal_connect (window->calendar_chats, "month-changed",
			  G_CALLBACK (log_window_calendar_chats_month_changed_cb),
			  window);

	/* Configure Search EmpathyChatView */
	window->chatview_find = empathy_theme_manager_create_view (empathy_theme_manager_get ());
	gtk_container_add (GTK_CONTAINER (window->scrolledwindow_find),
			   GTK_WIDGET (window->chatview_find));
	gtk_widget_show (GTK_WIDGET (window->chatview_find));

	/* Configure Contacts EmpathyChatView */
	window->chatview_chats = empathy_theme_manager_create_view (empathy_theme_manager_get ());
	gtk_container_add (GTK_CONTAINER (window->scrolledwindow_chats),
			   GTK_WIDGET (window->chatview_chats));
	gtk_widget_show (GTK_WIDGET (window->chatview_chats));

	/* Account chooser for chats */
	window->account_chooser_chats = empathy_account_chooser_new ();
	account_chooser = EMPATHY_ACCOUNT_CHOOSER (window->account_chooser_chats);

	gtk_box_pack_start (GTK_BOX (window->vbox_chats),
			    window->account_chooser_chats,
			    FALSE, TRUE, 0);

	g_signal_connect (window->account_chooser_chats, "changed",
			  G_CALLBACK (log_window_chats_accounts_changed_cb),
			  window);

	/* Populate */
	account_manager = tp_account_manager_dup ();
	tp_account_manager_prepare_async (account_manager, NULL,
					  account_manager_prepared_cb, window);
	g_object_unref (account_manager);

	/* Search List */
	log_window_find_setup (window);

	/* Contacts */
	log_window_chats_setup (window);
	log_window_chats_populate (window);

	if (account != NULL && chat_id != NULL)
		select_account_once_ready (window, account, chat_id, is_chatroom);

	if (parent != NULL) {
		gtk_window_set_transient_for (GTK_WINDOW (window->window),
					      GTK_WINDOW (parent));
	}

	gtk_widget_show (window->window);

	return window->window;
}

static void
log_window_destroy_cb (GtkWidget       *widget,
		       EmpathyLogWindow *window)
{
	g_free (window->last_find);
	g_object_unref (window->log_manager);
	tp_clear_object (&window->selected_account);
	g_free (window->selected_chat_id);

	g_free (window);
}

/*
 * Search code.
 */
static void
log_window_entry_find_changed_cb (GtkWidget       *entry,
				  EmpathyLogWindow *window)
{
	const gchar *str;
	gboolean     is_sensitive = TRUE;

	str = gtk_entry_get_text (GTK_ENTRY (window->entry_find));

	is_sensitive &= !EMP_STR_EMPTY (str);
	is_sensitive &=
		!window->last_find ||
		(window->last_find && tp_strdiff (window->last_find, str));

	gtk_widget_set_sensitive (window->button_find, is_sensitive);
}

static void
got_messages_for_date_cb (GObject *manager,
                       GAsyncResult *result,
                       gpointer user_data)
{
	EmpathyLogWindow *window = user_data;
	GList         *messages;
	GList         *l;
	gboolean       can_do_previous;
	gboolean       can_do_next;
	GError        *error = NULL;

	if (log_window == NULL)
		return;

	if (!tpl_log_manager_get_messages_for_date_finish (TPL_LOG_MANAGER (manager),
		result, &messages, &error)) {
			DEBUG ("Unable to retrieve messages for the selected date: %s. Aborting",
					error->message);
			empathy_chat_view_append_event (window->chatview_find,
					"Unable to retrieve messages for the selected date");
			g_error_free (error);
			return;
	}

	for (l = messages; l; l = l->next) {
			EmpathyMessage *message;

			g_assert (TPL_IS_ENTRY (l->data));

			message = empathy_message_from_tpl_log_entry (l->data);
			g_object_unref (l->data);
			empathy_chat_view_append_message (window->chatview_find, message);
			g_object_unref (message);
	}
	g_list_free (messages);

	/* Scroll to the most recent messages */
	empathy_chat_view_scroll (window->chatview_find, TRUE);

	/* Highlight and find messages */
	empathy_chat_view_highlight (window->chatview_find,
			window->last_find,
			FALSE);
	empathy_chat_view_find_next (window->chatview_find,
			window->last_find,
			TRUE,
			FALSE);
	empathy_chat_view_find_abilities (window->chatview_find,
			window->last_find,
			FALSE,
			&can_do_previous,
			&can_do_next);
	gtk_widget_set_sensitive (window->button_previous, can_do_previous);
	gtk_widget_set_sensitive (window->button_next, can_do_next);
	gtk_widget_set_sensitive (window->button_find, FALSE);
}

static GDate *
gdate_from_str (const gchar *str)
{
	guint u;
	guint day, month, year;

	if (sscanf (str, "%u", &u) != 1)
		return NULL;

	day = (u % 100);
	month = ((u / 100) % 100);
	year = (u / 10000);

	if (!g_date_valid_dmy (day, month, year))
		return NULL;

	return g_date_new_dmy (day, month, year);
}

static void
log_window_find_changed_cb (GtkTreeSelection *selection,
			    EmpathyLogWindow  *window)
{
	GtkTreeView   *view;
	GtkTreeModel  *model;
	GtkTreeIter    iter;
	TpAccount     *account;
	gchar         *chat_id;
	gboolean       is_chatroom;
	gchar         *date;
	GDate         *gdate;

	/* Get selected information */
	view = GTK_TREE_VIEW (window->treeview_find);
	model = gtk_tree_view_get_model (view);

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		gtk_widget_set_sensitive (window->button_previous, FALSE);
		gtk_widget_set_sensitive (window->button_next, FALSE);

		empathy_chat_view_clear (window->chatview_find);

		return;
	}

	gtk_widget_set_sensitive (window->button_previous, TRUE);
	gtk_widget_set_sensitive (window->button_next, TRUE);

	gtk_tree_model_get (model, &iter,
			    COL_FIND_ACCOUNT, &account,
			    COL_FIND_CHAT_ID, &chat_id,
			    COL_FIND_IS_CHATROOM, &is_chatroom,
			    COL_FIND_DATE, &date,
			    -1);

	/* Clear all current messages shown in the textview */
	empathy_chat_view_clear (window->chatview_find);

	/* Turn off scrolling temporarily */
	empathy_chat_view_scroll (window->chatview_find, FALSE);

	/* Get messages */
	gdate = gdate_from_str (date);

	if (gdate != NULL) {
		tpl_log_manager_get_messages_for_date_async (window->log_manager,
								      account,
								      chat_id,
								      is_chatroom,
								      gdate,
								      got_messages_for_date_cb,
								      window);

		g_date_free (gdate);
	}

	g_object_unref (account);
	g_free (date);
	g_free (chat_id);
}


static void
log_manager_searched_new_cb (GObject *manager,
                             GAsyncResult *result,
                             gpointer user_data)
{
	GList               *hits;
	GList               *l;
	GtkTreeIter          iter;
	GtkListStore        *store = user_data;
	GError              *error = NULL;

	if (log_window == NULL)
		return;

	if (!tpl_log_manager_search_finish (TPL_LOG_MANAGER (manager), result,
		&hits, &error)) {
			DEBUG ("%s. Aborting", error->message);
			g_error_free (error);
			return;
	}

	for (l = hits; l; l = l->next) {
			TplLogSearchHit *hit;
			const gchar         *account_name;
			const gchar         *account_icon;
			gchar               date_readable[255];
			gchar               tmp[255];

			hit = l->data;

			/* Protect against invalid data (corrupt or old log files. */
			if (hit->account == NULL || hit->chat_id == NULL) {
					continue;
			}

			g_date_strftime (date_readable, sizeof (date_readable),
				EMPATHY_DATE_FORMAT_DISPLAY_SHORT, hit->date);

			g_date_strftime (tmp, sizeof (tmp),
				"%Y%m%d", hit->date);

			account_name = tp_account_get_display_name (hit->account);
			account_icon = tp_account_get_icon_name (hit->account);

			gtk_list_store_append (store, &iter);
			gtk_list_store_set (store, &iter,
					COL_FIND_ACCOUNT_ICON, account_icon,
					COL_FIND_ACCOUNT_NAME, account_name,
					COL_FIND_ACCOUNT, hit->account,
					COL_FIND_CHAT_NAME, hit->chat_id, /* FIXME */
					COL_FIND_CHAT_ID, hit->chat_id,
					COL_FIND_IS_CHATROOM, hit->is_chatroom,
					COL_FIND_DATE, tmp,
					COL_FIND_DATE_READABLE, date_readable,
					-1);

			/* FIXME: Update COL_FIND_CHAT_NAME */
			if (hit->is_chatroom) {
			} else {
			}
	}

	if (hits != NULL) {
			tpl_log_manager_search_free (hits);
	}
}

static void
log_window_find_populate (EmpathyLogWindow *window,
			  const gchar     *search_criteria)
{
	GtkTreeView        *view;
	GtkTreeModel       *model;
	GtkTreeSelection   *selection;
	GtkListStore       *store;

	view = GTK_TREE_VIEW (window->treeview_find);
	model = gtk_tree_view_get_model (view);
	selection = gtk_tree_view_get_selection (view);
	store = GTK_LIST_STORE (model);

	empathy_chat_view_clear (window->chatview_find);

	gtk_list_store_clear (store);

	if (EMP_STR_EMPTY (search_criteria)) {
		/* Just clear the search. */
		return;
	}

	tpl_log_manager_search_async (window->log_manager, search_criteria,
			log_manager_searched_new_cb, (gpointer) store);
}

static void
log_window_find_setup (EmpathyLogWindow *window)
{
	GtkTreeView       *view;
	GtkTreeModel      *model;
	GtkTreeSelection  *selection;
	GtkTreeSortable   *sortable;
	GtkTreeViewColumn *column;
	GtkListStore      *store;
	GtkCellRenderer   *cell;
	gint               offset;

	view = GTK_TREE_VIEW (window->treeview_find);
	selection = gtk_tree_view_get_selection (view);

	/* New store */
	store = gtk_list_store_new (COL_FIND_COUNT,
				    G_TYPE_STRING,          /* account icon name */
				    G_TYPE_STRING,          /* account name */
				    TP_TYPE_ACCOUNT,        /* account */
				    G_TYPE_STRING,          /* chat name */
				    G_TYPE_STRING,          /* chat id */
				    G_TYPE_BOOLEAN,         /* is chatroom */
				    G_TYPE_STRING,          /* date */
				    G_TYPE_STRING);         /* date_readable */

	model = GTK_TREE_MODEL (store);
	sortable = GTK_TREE_SORTABLE (store);

	gtk_tree_view_set_model (view, model);

	/* New column */
	column = gtk_tree_view_column_new ();

	cell = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, cell, FALSE);
	gtk_tree_view_column_add_attribute (column, cell,
					    "icon-name",
					    COL_FIND_ACCOUNT_ICON);

	cell = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, cell, TRUE);
	gtk_tree_view_column_add_attribute (column, cell,
					    "text",
					    COL_FIND_ACCOUNT_NAME);

	gtk_tree_view_column_set_title (column, _("Account"));
	gtk_tree_view_append_column (view, column);

	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_clickable (column, TRUE);

	cell = gtk_cell_renderer_text_new ();
	offset = gtk_tree_view_insert_column_with_attributes (view, -1, _("Conversation"),
							      cell, "text", COL_FIND_CHAT_NAME,
							      NULL);

	column = gtk_tree_view_get_column (view, offset - 1);
	gtk_tree_view_column_set_sort_column_id (column, COL_FIND_CHAT_NAME);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_clickable (column, TRUE);

	cell = gtk_cell_renderer_text_new ();
	offset = gtk_tree_view_insert_column_with_attributes (view, -1, _("Date"),
							      cell, "text", COL_FIND_DATE_READABLE,
							      NULL);

	column = gtk_tree_view_get_column (view, offset - 1);
	gtk_tree_view_column_set_sort_column_id (column, COL_FIND_DATE);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_clickable (column, TRUE);

	/* Set up treeview properties */
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	gtk_tree_sortable_set_sort_column_id (sortable,
					      COL_FIND_DATE,
					      GTK_SORT_ASCENDING);

	/* Set up signals */
	g_signal_connect (selection, "changed",
			  G_CALLBACK (log_window_find_changed_cb),
			  window);

	g_object_unref (store);
}

static void
start_find_search (EmpathyLogWindow *window)
{
	const gchar *str;

	str = gtk_entry_get_text (GTK_ENTRY (window->entry_find));

	/* Don't find the same crap again */
	if (window->last_find && !tp_strdiff (window->last_find, str)) {
		return;
	}

	g_free (window->last_find);
	window->last_find = g_strdup (str);

	log_window_find_populate (window, str);
}

static void
log_window_button_find_clicked_cb (GtkWidget       *widget,
				   EmpathyLogWindow *window)
{
	start_find_search (window);
}

static void
log_window_entry_find_activate_cb (GtkWidget *entry,
				   EmpathyLogWindow *self)
{
	start_find_search (self);
}

static void
log_window_button_next_clicked_cb (GtkWidget       *widget,
				   EmpathyLogWindow *window)
{
	if (window->last_find) {
		gboolean can_do_previous;
		gboolean can_do_next;

		empathy_chat_view_find_next (window->chatview_find,
					    window->last_find,
					    FALSE,
					    FALSE);
		empathy_chat_view_find_abilities (window->chatview_find,
						 window->last_find,
						 FALSE,
						 &can_do_previous,
						 &can_do_next);
		gtk_widget_set_sensitive (window->button_previous, can_do_previous);
		gtk_widget_set_sensitive (window->button_next, can_do_next);
	}
}

static void
log_window_button_previous_clicked_cb (GtkWidget       *widget,
				       EmpathyLogWindow *window)
{
	if (window->last_find) {
		gboolean can_do_previous;
		gboolean can_do_next;

		empathy_chat_view_find_previous (window->chatview_find,
						window->last_find,
						FALSE,
						FALSE);
		empathy_chat_view_find_abilities (window->chatview_find,
						 window->last_find,
						 FALSE,
						 &can_do_previous,
						 &can_do_next);
		gtk_widget_set_sensitive (window->button_previous, can_do_previous);
		gtk_widget_set_sensitive (window->button_next, can_do_next);
	}
}

static void
log_window_button_close_clicked_cb (GtkWidget *widget,
				    EmpathyLogWindow *window)
{
	gtk_widget_destroy (window->window);
}

/*
 * Chats Code
 */

static void
log_window_chats_changed_cb (GtkTreeSelection *selection,
			     EmpathyLogWindow  *window)
{
	/* Use last date by default */
	gtk_calendar_clear_marks (GTK_CALENDAR (window->calendar_chats));

	log_window_chats_get_messages (window, NULL);
}

static void
log_manager_got_chats_cb (GObject *manager,
                       GAsyncResult *result,
                       gpointer user_data)
{
	EmpathyLogWindow      *window = user_data;
	GList                 *chats;
	GList                 *l;
	GtkTreeView           *view;
	GtkTreeModel          *model;
	GtkTreeSelection      *selection;
	GtkListStore          *store;
	GtkTreeIter            iter;
	GError                *error = NULL;
	gboolean               select_account = FALSE;

	if (log_window == NULL)
		return;

	if (!tpl_log_manager_get_chats_finish (TPL_LOG_MANAGER (manager),
		result, &chats, &error)) {
			DEBUG ("%s. Aborting", error->message);
			g_error_free (error);
			return;
	}

	view = GTK_TREE_VIEW (window->treeview_chats);
	model = gtk_tree_view_get_model (view);
	selection = gtk_tree_view_get_selection (view);
	store = GTK_LIST_STORE (model);

	for (l = chats; l; l = l->next) {
			TplLogSearchHit *hit;

			hit = l->data;

			if (hit->account == NULL)
				continue;

			gtk_list_store_append (store, &iter);
			gtk_list_store_set (store, &iter,
					COL_CHAT_ICON, "empathy-available", /* FIXME */
					COL_CHAT_NAME, hit->chat_id,
					COL_CHAT_ACCOUNT, hit->account,
					COL_CHAT_ID, hit->chat_id,
					COL_CHAT_IS_CHATROOM, hit->is_chatroom,
					-1);

			if (window->selected_account != NULL &&
			    !tp_strdiff (tp_proxy_get_object_path (hit->account),
			    tp_proxy_get_object_path (window->selected_account)))
				select_account = TRUE;

			/* FIXME: Update COL_CHAT_ICON/NAME */
			if (hit->is_chatroom) {
			} else {
			}
	}
	tpl_log_manager_search_free (chats);

	/* Unblock signals */
	g_signal_handlers_unblock_by_func (selection,
			log_window_chats_changed_cb,
			window);

	/* We display the selected account if we populate the model with chats from
	 * this account. */
	if (select_account)
		log_window_chats_set_selected (window);
}

static void
log_window_chats_populate (EmpathyLogWindow *window)
{
	EmpathyAccountChooser *account_chooser;
	TpAccount             *account;

	GtkTreeView          *view;
	GtkTreeModel         *model;
	GtkTreeSelection     *selection;
	GtkListStore         *store;

	account_chooser = EMPATHY_ACCOUNT_CHOOSER (window->account_chooser_chats);
	account = empathy_account_chooser_dup_account (account_chooser);

	view = GTK_TREE_VIEW (window->treeview_chats);
	model = gtk_tree_view_get_model (view);
	selection = gtk_tree_view_get_selection (view);
	store = GTK_LIST_STORE (model);

	if (account == NULL) {
		gtk_list_store_clear (store);
		return;
	}

	/* Block signals to stop the logs being retrieved prematurely */
	g_signal_handlers_block_by_func (selection,
					 log_window_chats_changed_cb,
					 window);

	gtk_list_store_clear (store);

	tpl_log_manager_get_chats_async (window->log_manager, account,
			log_manager_got_chats_cb, (gpointer) window);
}

static void
log_window_chats_setup (EmpathyLogWindow *window)
{
	GtkTreeView       *view;
	GtkTreeModel      *model;
	GtkTreeSelection  *selection;
	GtkTreeSortable   *sortable;
	GtkTreeViewColumn *column;
	GtkListStore      *store;
	GtkCellRenderer   *cell;

	view = GTK_TREE_VIEW (window->treeview_chats);
	selection = gtk_tree_view_get_selection (view);

	/* new store */
	store = gtk_list_store_new (COL_CHAT_COUNT,
				    G_TYPE_STRING,        /* icon */
				    G_TYPE_STRING,        /* name */
				    TP_TYPE_ACCOUNT,      /* account */
				    G_TYPE_STRING,        /* id */
				    G_TYPE_BOOLEAN);      /* is chatroom */

	model = GTK_TREE_MODEL (store);
	sortable = GTK_TREE_SORTABLE (store);

	gtk_tree_view_set_model (view, model);

	/* new column */
	column = gtk_tree_view_column_new ();

	cell = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, cell, FALSE);
	gtk_tree_view_column_add_attribute (column, cell,
					    "icon-name",
					    COL_CHAT_ICON);

	cell = gtk_cell_renderer_text_new ();
	g_object_set (cell, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	gtk_tree_view_column_pack_start (column, cell, TRUE);
	gtk_tree_view_column_add_attribute (column, cell,
					    "text",
					    COL_CHAT_NAME);

	gtk_tree_view_append_column (view, column);

	/* set up treeview properties */
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	gtk_tree_sortable_set_sort_column_id (sortable,
					      COL_CHAT_NAME,
					      GTK_SORT_ASCENDING);

	/* set up signals */
	g_signal_connect (selection, "changed",
			  G_CALLBACK (log_window_chats_changed_cb),
			  window);

	g_object_unref (store);
}

static void
log_window_chats_accounts_changed_cb (GtkWidget       *combobox,
				      EmpathyLogWindow *window)
{
	/* Clear all current messages shown in the textview */
	empathy_chat_view_clear (window->chatview_chats);

	log_window_chats_populate (window);
}

static void
log_window_chats_set_selected (EmpathyLogWindow *window)
{
	GtkTreeView          *view;
	GtkTreeModel         *model;
	GtkTreeSelection     *selection;
	GtkTreeIter           iter;
	GtkTreePath          *path;
	gboolean              ok;

	view = GTK_TREE_VIEW (window->treeview_chats);
	model = gtk_tree_view_get_model (view);
	selection = gtk_tree_view_get_selection (view);

	if (!gtk_tree_model_get_iter_first (model, &iter)) {
		return;
	}

	for (ok = TRUE; ok; ok = gtk_tree_model_iter_next (model, &iter)) {
		TpAccount *this_account;
		gchar     *this_chat_id;
		gboolean   this_is_chatroom;

		gtk_tree_model_get (model, &iter,
				    COL_CHAT_ACCOUNT, &this_account,
				    COL_CHAT_ID, &this_chat_id,
				    COL_CHAT_IS_CHATROOM, &this_is_chatroom,
				    -1);

		if (this_account == window->selected_account &&
		    !tp_strdiff (this_chat_id, window->selected_chat_id) &&
		    this_is_chatroom == window->selected_is_chatroom) {
			gtk_tree_selection_select_iter (selection, &iter);
			path = gtk_tree_model_get_path (model, &iter);
			gtk_tree_view_scroll_to_cell (view, path, NULL, TRUE, 0.5, 0.0);
			gtk_tree_path_free (path);
			g_object_unref (this_account);
			g_free (this_chat_id);
			break;
		}

		g_object_unref (this_account);
		g_free (this_chat_id);
	}

	tp_clear_object (&window->selected_account);
	tp_clear_pointer (&window->selected_chat_id, g_free);
}

static gboolean
log_window_chats_get_selected (EmpathyLogWindow  *window,
			       TpAccount       **account,
			       gchar           **chat_id,
			       gboolean         *is_chatroom)
{
	GtkTreeView      *view;
	GtkTreeModel     *model;
	GtkTreeSelection *selection;
	GtkTreeIter       iter;
	gchar            *id = NULL;
	TpAccount        *acc = NULL;
	gboolean          room = FALSE;

	view = GTK_TREE_VIEW (window->treeview_chats);
	model = gtk_tree_view_get_model (view);
	selection = gtk_tree_view_get_selection (view);

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		return FALSE;
	}

	gtk_tree_model_get (model, &iter,
			    COL_CHAT_ACCOUNT, &acc,
			    COL_CHAT_ID, &id,
			    COL_CHAT_IS_CHATROOM, &room,
			    -1);

	if (chat_id != NULL) {
		*chat_id = id;
	} else {
		g_free (id);
	}
	if (account != NULL) {
		*account = acc;
	} else {
		g_object_unref (acc);
	}
	if (is_chatroom) {
		*is_chatroom = room;
	}

	return TRUE;
}

static void
log_window_got_messages_for_date_cb (GObject *manager,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyLogWindow *window = user_data;
  GList *messages;
  GList *l;
  GError *error = NULL;

  if (log_window == NULL)
    return;

  if (!tpl_log_manager_get_messages_for_date_finish (TPL_LOG_MANAGER (manager),
        result, &messages, &error)) {
      DEBUG ("Unable to retrieve messages for the selected date: %s. Aborting",
          error->message);
      empathy_chat_view_append_event (window->chatview_find,
          "Unable to retrieve messages for the selected date");
      g_error_free (error);
      return;
  }

  for (l = messages; l; l = l->next) {
      EmpathyMessage *message = empathy_message_from_tpl_log_entry (l->data);
      g_object_unref (l->data);
      empathy_chat_view_append_message (window->chatview_chats,
          message);
      g_object_unref (message);
  }
  g_list_free (messages);

  /* Turn back on scrolling */
  empathy_chat_view_scroll (window->chatview_find, TRUE);

  /* Give the search entry main focus */
  gtk_widget_grab_focus (window->entry_chats);
}


static void
log_window_get_messages_for_date (EmpathyLogWindow *window,
				  GDate *date)
{
  TpAccount *account;
  gchar *chat_id;
  gboolean is_chatroom;

  if (!log_window_chats_get_selected (window, &account,
        &chat_id, &is_chatroom)) {
      return;
  }

  /* Clear all current messages shown in the textview */
  empathy_chat_view_clear (window->chatview_chats);

  /* Turn off scrolling temporarily */
  empathy_chat_view_scroll (window->chatview_find, FALSE);

  /* Get messages */
  tpl_log_manager_get_messages_for_date_async (window->log_manager,
      account, chat_id,
      is_chatroom,
      date,
      log_window_got_messages_for_date_cb,
      (gpointer) window);
}

static void
log_manager_got_dates_cb (GObject *manager,
                          GAsyncResult *result,
                          gpointer user_data)
{
  EmpathyLogWindow *window = user_data;
  GList         *dates;
  GList         *l;
  guint          year_selected;
  guint          month_selected;
  gboolean       day_selected = FALSE;
  GDate         *date = NULL;
  GError        *error = NULL;

  if (log_window == NULL)
    return;

  if (!tpl_log_manager_get_dates_finish (TPL_LOG_MANAGER (manager),
        result, &dates, &error)) {
    DEBUG ("Unable to retrieve messages' dates: %s. Aborting",
        error->message);
    empathy_chat_view_append_event (window->chatview_find,
        "Unable to retrieve messages' dates");
      return;
  }

  for (l = dates; l; l = l->next) {
      GDate *d = l->data;

      gtk_calendar_get_date (GTK_CALENDAR (window->calendar_chats),
          &year_selected,
          &month_selected,
          NULL);

      month_selected++;

      if (!l->next) {
          date = d;
      }

      if (g_date_get_year (d) != year_selected ||
          g_date_get_month (d) != month_selected) {
          continue;
      }

      DEBUG ("Marking date: %04u-%02u-%02u", g_date_get_year (d),
          g_date_get_month (d), g_date_get_day (d));

      gtk_calendar_mark_day (GTK_CALENDAR (window->calendar_chats),
          g_date_get_day (d));

      if (l->next) {
          continue;
      }

      day_selected = TRUE;

      gtk_calendar_select_day (GTK_CALENDAR (window->calendar_chats),
          g_date_get_day (d));
  }

  if (!day_selected) {
      /* Unselect the day in the calendar */
      gtk_calendar_select_day (GTK_CALENDAR (window->calendar_chats), 0);
  }

  g_signal_handlers_unblock_by_func (window->calendar_chats,
      log_window_calendar_chats_day_selected_cb,
      window);

  if (date != NULL) {
      /* Show messages of the most recent date */
      log_window_get_messages_for_date (window, date);
  }

  g_list_foreach (dates, (GFunc) g_free, NULL);
  g_list_free (dates);
}


static void
log_window_chats_get_messages (EmpathyLogWindow *window,
			       GDate     *date)
{
	TpAccount     *account;
	gchar         *chat_id;
	gboolean       is_chatroom;
	guint          year_selected;
	guint          month_selected;
	guint          day;


	if (!log_window_chats_get_selected (window, &account,
					    &chat_id, &is_chatroom)) {
		return;
	}

	g_signal_handlers_block_by_func (window->calendar_chats,
					 log_window_calendar_chats_day_selected_cb,
					 window);

	/* Either use the supplied date or get the last */
	if (date == NULL) {
		/* Get a list of dates and show them on the calendar */
		tpl_log_manager_get_dates_async (window->log_manager,
						       account, chat_id,
						       is_chatroom,
						       log_manager_got_dates_cb, (gpointer) window);
    /* signal unblocked at the end of the CB flow */
	} else {
		day = g_date_get_day (date);
		gtk_calendar_get_date (GTK_CALENDAR (window->calendar_chats),
				&year_selected,
				&month_selected,
				NULL);

		month_selected++;

		if (g_date_get_year (date) != year_selected &&
			g_date_get_month (date) != month_selected) {
			day = 0;
		}

		gtk_calendar_select_day (GTK_CALENDAR (window->calendar_chats), day);

    g_signal_handlers_unblock_by_func (window->calendar_chats,
        log_window_calendar_chats_day_selected_cb,
        window);
	}

	if (date != NULL) {
		/* Show messages of the selected date */
		log_window_get_messages_for_date (window, date);
	}

	g_object_unref (account);
	g_free (chat_id);
}

static void
log_window_calendar_chats_day_selected_cb (GtkWidget       *calendar,
					   EmpathyLogWindow *window)
{
	guint  year;
	guint  month;
	guint  day;
	GDate *date;

	gtk_calendar_get_date (GTK_CALENDAR (calendar), &year, &month, &day);
	if (day == 0)
		/* No date selected */
		return;

	/* We need this hear because it appears that the months start from 0 */
	month++;

	date = g_date_new_dmy (day, month, year);

	DEBUG ("Currently selected date is: %04u-%02u-%02u", year, month, day);

	log_window_chats_get_messages (window, date);

	g_date_free (date);
}

static void
log_window_updating_calendar_month_cb (GObject *manager,
		GAsyncResult *result, gpointer user_data)
{
	EmpathyLogWindow *window = user_data;
	GList					*dates;
	GList					*l;
	guint					 year_selected;
	guint					 month_selected;
	GError				*error = NULL;

	if (log_window == NULL)
		return;

	if (!tpl_log_manager_get_dates_finish (TPL_LOG_MANAGER (manager),
		result, &dates, &error)) {
			DEBUG ("Unable to retrieve messages' dates: %s. Aborting",
					error->message);
			empathy_chat_view_append_event (window->chatview_find,
					"Unable to retrieve messages' dates");
			g_error_free (error);
			return;
	}

	gtk_calendar_clear_marks (GTK_CALENDAR (window->calendar_chats));
	g_object_get (window->calendar_chats,
			"month", &month_selected,
			"year", &year_selected,
			NULL);

	/* We need this here because it appears that the months start from 0 */
	month_selected++;

	for (l = dates; l; l = l->next) {
			GDate *date = l->data;

			if (g_date_get_year (date) == year_selected &&
			    g_date_get_month (date) == month_selected) {
					DEBUG ("Marking date: %04u-%02u-%02u", g_date_get_year (date),
						g_date_get_month (date), g_date_get_day (date));
					gtk_calendar_mark_day (GTK_CALENDAR (window->calendar_chats), g_date_get_day (date));
			}
	}

	g_list_foreach (dates, (GFunc) g_free, NULL);
	g_list_free (dates);

	DEBUG ("Currently showing month %d and year %d", month_selected,
			year_selected);
}

static void
log_window_calendar_chats_month_changed_cb (GtkWidget       *calendar,
					    EmpathyLogWindow *window)
{
	TpAccount     *account;
	gchar         *chat_id;
	gboolean       is_chatroom;

	gtk_calendar_clear_marks (GTK_CALENDAR (calendar));

	if (!log_window_chats_get_selected (window, &account,
					    &chat_id, &is_chatroom)) {
		DEBUG ("No chat selected to get dates for...");
		return;
	}

	/* Get the log object for this contact */
	tpl_log_manager_get_dates_async (window->log_manager, account,
					       chat_id, is_chatroom,
					       log_window_updating_calendar_month_cb,
					       (gpointer) window);

	g_object_unref (account);
	g_free (chat_id);
}

static void
log_window_entry_chats_changed_cb (GtkWidget       *entry,
				   EmpathyLogWindow *window)
{
	const gchar *str;

	str = gtk_entry_get_text (GTK_ENTRY (window->entry_chats));
	empathy_chat_view_highlight (window->chatview_chats, str, FALSE);

	if (str != NULL) {
		empathy_chat_view_find_next (window->chatview_chats,
					    str,
					    TRUE,
					    FALSE);
	}
}

static void
log_window_entry_chats_activate_cb (GtkWidget       *entry,
				    EmpathyLogWindow *window)
{
	const gchar *str;

	str = gtk_entry_get_text (GTK_ENTRY (window->entry_chats));

	if (str != NULL) {
		empathy_chat_view_find_next (window->chatview_chats,
					    str,
					    FALSE,
					    FALSE);
	}
}
