/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2002-2007 Imendio AB
 * Copyright (C) 2007-2010 Collabora Ltd.
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
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#include <config.h>

#include <sys/stat.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/util.h>
#include <folks/folks.h>

#include <libempathy/empathy-contact.h>
#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-request-util.h>
#include <libempathy/empathy-chatroom-manager.h>
#include <libempathy/empathy-chatroom.h>
#include <libempathy/empathy-contact-list.h>
#include <libempathy/empathy-contact-manager.h>
#include <libempathy/empathy-gsettings.h>
#include <libempathy/empathy-individual-manager.h>
#include <libempathy/empathy-gsettings.h>
#include <libempathy/empathy-status-presets.h>
#include <libempathy/empathy-tp-contact-factory.h>

#include <libempathy-gtk/empathy-contact-dialogs.h>
#include <libempathy-gtk/empathy-live-search.h>
#include <libempathy-gtk/empathy-contact-blocking-dialog.h>
#include <libempathy-gtk/empathy-contact-search-dialog.h>
#include <libempathy-gtk/empathy-geometry.h>
#include <libempathy-gtk/empathy-gtk-enum-types.h>
#include <libempathy-gtk/empathy-individual-dialogs.h>
#include <libempathy-gtk/empathy-individual-store.h>
#include <libempathy-gtk/empathy-individual-store-manager.h>
#include <libempathy-gtk/empathy-individual-view.h>
#include <libempathy-gtk/empathy-new-message-dialog.h>
#include <libempathy-gtk/empathy-new-call-dialog.h>
#include <libempathy-gtk/empathy-log-window.h>
#include <libempathy-gtk/empathy-presence-chooser.h>
#include <libempathy-gtk/empathy-sound-manager.h>
#include <libempathy-gtk/empathy-ui-utils.h>

#include "empathy-accounts-dialog.h"
#include "empathy-call-observer.h"
#include "empathy-chat-manager.h"
#include "empathy-main-window.h"
#include "empathy-preferences.h"
#include "empathy-about-dialog.h"
#include "empathy-debug-window.h"
#include "empathy-new-chatroom-dialog.h"
#include "empathy-map-view.h"
#include "empathy-chatrooms-window.h"
#include "empathy-event-manager.h"
#include "empathy-ft-manager.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

/* Flashing delay for icons (milliseconds). */
#define FLASH_TIMEOUT 500

/* Minimum width of roster window if something goes wrong. */
#define MIN_WIDTH 50

/* Accels (menu shortcuts) can be configured and saved */
#define ACCELS_FILENAME "accels.txt"

/* Name in the geometry file */
#define GEOMETRY_NAME "main-window"

enum {
	PAGE_CONTACT_LIST = 0,
	PAGE_NO_MATCH
};

enum {
	PROP_0,
	PROP_SHELL_RUNNING
};

G_DEFINE_TYPE (EmpathyMainWindow, empathy_main_window, GTK_TYPE_WINDOW);

#define GET_PRIV(self) ((EmpathyMainWindowPriv *)((EmpathyMainWindow *) self)->priv)

struct _EmpathyMainWindowPriv {
	EmpathyContactList      *contact_manager;
	EmpathyIndividualStore  *individual_store;
	EmpathyIndividualView   *individual_view;
	TpAccountManager        *account_manager;
	EmpathyChatroomManager  *chatroom_manager;
	EmpathyEventManager     *event_manager;
	EmpathySoundManager     *sound_mgr;
	EmpathyCallObserver     *call_observer;
	guint                    flash_timeout_id;
	gboolean                 flash_on;
	gboolean                 empty;

	GSettings              *gsettings_ui;
	GSettings              *gsettings_contacts;

	GtkWidget              *preferences;
	GtkWidget              *main_vbox;
	GtkWidget              *throbber;
	GtkWidget              *throbber_tool_item;
	GtkWidget              *presence_toolbar;
	GtkWidget              *presence_chooser;
	GtkWidget              *errors_vbox;
	GtkWidget              *auth_vbox;
	GtkWidget              *search_bar;
	GtkWidget              *notebook;
	GtkWidget              *no_entry_label;

	GtkToggleAction        *show_protocols;
	GtkRadioAction         *sort_by_name;
	GtkRadioAction         *sort_by_status;
	GtkRadioAction         *normal_with_avatars;
	GtkRadioAction         *normal_size;
	GtkRadioAction         *compact_size;

	GtkUIManager           *ui_manager;
	GtkAction              *view_history;
	GtkAction              *room_join_favorites;
	GtkWidget              *room_menu;
	GtkWidget              *room_separator;
	GtkWidget              *edit_context;
	GtkWidget              *edit_context_separator;

	GtkActionGroup         *balance_action_group;
	GtkAction              *view_balance_show_in_roster;
	GtkWidget              *balance_vbox;

	guint                   size_timeout_id;

	/* reffed TpAccount* => visible GtkInfoBar* */
	GHashTable             *errors;

	/* EmpathyEvent* => visible GtkInfoBar* */
	GHashTable             *auths;

	/* stores a mapping from TpAccount to Handler ID to prevent
	 * to listen more than once to the status-changed signal */
	GHashTable             *status_changed_handlers;

	/* Actions that are enabled when there are connected accounts */
	GList                  *actions_connected;

	gboolean               shell_running;
};

static void
main_window_flash_stop (EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);

	if (priv->flash_timeout_id == 0) {
		return;
	}

	DEBUG ("Stop flashing");
	g_source_remove (priv->flash_timeout_id);
	priv->flash_timeout_id = 0;
	priv->flash_on = FALSE;
}

typedef struct {
	EmpathyEvent       *event;
	gboolean            on;
	EmpathyMainWindow  *window;
} FlashForeachData;

static gboolean
main_window_flash_foreach (GtkTreeModel *model,
			   GtkTreePath  *path,
			   GtkTreeIter  *iter,
			   gpointer      user_data)
{
	FlashForeachData *data = (FlashForeachData *) user_data;
	FolksIndividual *individual;
	EmpathyContact   *contact;
	const gchar      *icon_name;
	GtkTreePath      *parent_path = NULL;
	GtkTreeIter       parent_iter;
	GdkPixbuf        *pixbuf = NULL;

	gtk_tree_model_get (model, iter,
			    EMPATHY_INDIVIDUAL_STORE_COL_INDIVIDUAL, &individual,
			    -1);

	if (individual == NULL)
		return FALSE;

	contact = empathy_contact_dup_from_folks_individual (individual);
	if (contact != data->event->contact)
		goto out;

	if (data->on) {
		icon_name = data->event->icon_name;
		pixbuf = empathy_pixbuf_from_icon_name (icon_name, GTK_ICON_SIZE_MENU);
	} else {
		pixbuf = empathy_individual_store_get_individual_status_icon (
						GET_PRIV (data->window)->individual_store,
						individual);
		if (pixbuf != NULL)
			g_object_ref (pixbuf);
	}

	gtk_tree_store_set (GTK_TREE_STORE (model), iter,
			    EMPATHY_INDIVIDUAL_STORE_COL_ICON_STATUS, pixbuf,
			    -1);

	/* To make sure the parent is shown correctly, we emit
	 * the row-changed signal on the parent so it prompts
	 * it to be refreshed by the filter func.
	 */
	if (gtk_tree_model_iter_parent (model, &parent_iter, iter)) {
		parent_path = gtk_tree_model_get_path (model, &parent_iter);
	}
	if (parent_path) {
		gtk_tree_model_row_changed (model, parent_path, &parent_iter);
		gtk_tree_path_free (parent_path);
	}

out:
	g_object_unref (individual);
	tp_clear_object (&contact);
	tp_clear_object (&pixbuf);

	return FALSE;
}

static gboolean
main_window_flash_cb (EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GtkTreeModel     *model;
	GSList           *events, *l;
	gboolean          found_event = FALSE;
	FlashForeachData  data;

	priv->flash_on = !priv->flash_on;
	data.on = priv->flash_on;
	model = GTK_TREE_MODEL (priv->individual_store);

	events = empathy_event_manager_get_events (priv->event_manager);
	for (l = events; l; l = l->next) {
		data.event = l->data;
		data.window = window;
		if (!data.event->contact || !data.event->must_ack) {
			continue;
		}

		found_event = TRUE;
		gtk_tree_model_foreach (model,
					main_window_flash_foreach,
					&data);
	}

	if (!found_event) {
		main_window_flash_stop (window);
	}

	return TRUE;
}

static void
main_window_flash_start (EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);

	if (priv->flash_timeout_id != 0) {
		return;
	}

	DEBUG ("Start flashing");
	priv->flash_timeout_id = g_timeout_add (FLASH_TIMEOUT,
						(GSourceFunc) main_window_flash_cb,
						window);
	main_window_flash_cb (window);
}

static void
main_window_remove_auth (EmpathyMainWindow *window,
			 EmpathyEvent      *event)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GtkWidget *error_widget;

	error_widget = g_hash_table_lookup (priv->auths, event);
	if (error_widget != NULL) {
		gtk_widget_destroy (error_widget);
		g_hash_table_remove (priv->auths, event);
	}
}

static void
main_window_auth_add_clicked_cb (GtkButton         *button,
				 EmpathyMainWindow *window)
{
	EmpathyEvent *event;

	event = g_object_get_data (G_OBJECT (button), "event");

	empathy_event_approve (event);

	main_window_remove_auth (window, event);
}

static void
main_window_auth_close_clicked_cb (GtkButton         *button,
				   EmpathyMainWindow *window)
{
	EmpathyEvent *event;

	event = g_object_get_data (G_OBJECT (button), "event");

	empathy_event_decline (event);
	main_window_remove_auth (window, event);
}

static void
main_window_auth_display (EmpathyMainWindow *window,
			  EmpathyEvent      *event)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	TpAccount *account = event->account;
	GtkWidget *info_bar;
	GtkWidget *content_area;
	GtkWidget *image;
	GtkWidget *label;
	GtkWidget *add_button;
	GtkWidget *close_button;
	GtkWidget *action_area;
	GtkWidget *action_grid;
	const gchar *icon_name;
	gchar *str;

	if (g_hash_table_lookup (priv->auths, event) != NULL) {
		return;
	}

	info_bar = gtk_info_bar_new ();
	gtk_info_bar_set_message_type (GTK_INFO_BAR (info_bar), GTK_MESSAGE_QUESTION);

	gtk_widget_set_no_show_all (info_bar, TRUE);
	gtk_box_pack_start (GTK_BOX (priv->auth_vbox), info_bar, FALSE, TRUE, 0);
	gtk_widget_show (info_bar);

	icon_name = tp_account_get_icon_name (account);
	image = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_widget_show (image);

	str = g_markup_printf_escaped ("<b>%s</b>\n%s",
				       tp_account_get_display_name (account),
				       _("Password required"));

	label = gtk_label_new (str);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_widget_show (label);

	g_free (str);

	content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (info_bar));
	gtk_box_pack_start (GTK_BOX (content_area), image, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (content_area), label, TRUE, TRUE, 0);

	image = gtk_image_new_from_stock (GTK_STOCK_ADD, GTK_ICON_SIZE_BUTTON);
	add_button = gtk_button_new ();
	gtk_button_set_image (GTK_BUTTON (add_button), image);
	gtk_widget_set_tooltip_text (add_button, _("Provide Password"));
	gtk_widget_show (add_button);

	image = gtk_image_new_from_stock (GTK_STOCK_CLOSE, GTK_ICON_SIZE_BUTTON);
	close_button = gtk_button_new ();
	gtk_button_set_image (GTK_BUTTON (close_button), image);
	gtk_widget_set_tooltip_text (close_button, _("Disconnect"));
	gtk_widget_show (close_button);

	action_grid = gtk_grid_new ();
	gtk_grid_set_column_spacing (GTK_GRID (action_grid), 6);
	gtk_widget_show (action_grid);

	action_area = gtk_info_bar_get_action_area (GTK_INFO_BAR (info_bar));
	gtk_box_pack_start (GTK_BOX (action_area), action_grid, FALSE, FALSE, 0);

	gtk_grid_attach (GTK_GRID (action_grid), add_button, 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (action_grid), close_button, 1, 0, 1, 1);

	g_object_set_data_full (G_OBJECT (info_bar),
				"event", event, NULL);
	g_object_set_data_full (G_OBJECT (add_button),
				"event", event, NULL);
	g_object_set_data_full (G_OBJECT (close_button),
				"event", event, NULL);

	g_signal_connect (add_button, "clicked",
			  G_CALLBACK (main_window_auth_add_clicked_cb),
			  window);
	g_signal_connect (close_button, "clicked",
			  G_CALLBACK (main_window_auth_close_clicked_cb),
			  window);

	gtk_widget_show (priv->auth_vbox);

	g_hash_table_insert (priv->auths, event, info_bar);
}

static void
modify_event_count (GtkTreeModel *model,
		    GtkTreeIter *iter,
		    EmpathyEvent *event,
		    gboolean increase)
{
	FolksIndividual *individual;
	EmpathyContact *contact;
	guint count;

	gtk_tree_model_get (model, iter,
			    EMPATHY_INDIVIDUAL_STORE_COL_INDIVIDUAL, &individual,
			    EMPATHY_INDIVIDUAL_STORE_COL_EVENT_COUNT, &count,
			    -1);

	if (individual == NULL)
		return;

	increase ? count++ : count--;

	contact = empathy_contact_dup_from_folks_individual (individual);
	if (contact == event->contact) {
		gtk_tree_store_set (GTK_TREE_STORE (model), iter,
				    EMPATHY_INDIVIDUAL_STORE_COL_EVENT_COUNT, count, -1);
	}

	tp_clear_object (&contact);
	g_object_unref (individual);
}

static gboolean
increase_event_count_foreach (GtkTreeModel *model,
			      GtkTreePath *path,
			      GtkTreeIter *iter,
			      gpointer user_data)
{
	EmpathyEvent *event = user_data;

	modify_event_count (model, iter, event, TRUE);

	return FALSE;
}

static void
increase_event_count (EmpathyMainWindow *self,
		      EmpathyEvent *event)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (self);
	GtkTreeModel *model;

	model = GTK_TREE_MODEL (priv->individual_store);

	gtk_tree_model_foreach (model, increase_event_count_foreach, event);
}

static gboolean
decrease_event_count_foreach (GtkTreeModel *model,
			      GtkTreePath *path,
			      GtkTreeIter *iter,
			      gpointer user_data)
{
	EmpathyEvent *event = user_data;

	modify_event_count (model, iter, event, FALSE);

	return FALSE;
}

static void
decrease_event_count (EmpathyMainWindow *self,
		      EmpathyEvent *event)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (self);
	GtkTreeModel *model;

	model = GTK_TREE_MODEL (priv->individual_store);

	gtk_tree_model_foreach (model, decrease_event_count_foreach, event);
}

static void
main_window_event_added_cb (EmpathyEventManager *manager,
			    EmpathyEvent        *event,
			    EmpathyMainWindow   *window)
{
	if (event->contact) {
		increase_event_count (window, event);

		main_window_flash_start (window);
	} else if (event->type == EMPATHY_EVENT_TYPE_AUTH) {
		main_window_auth_display (window, event);
	}
}

static void
main_window_event_removed_cb (EmpathyEventManager *manager,
			      EmpathyEvent        *event,
			      EmpathyMainWindow   *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	FlashForeachData data;

	if (event->type == EMPATHY_EVENT_TYPE_AUTH) {
		main_window_remove_auth (window, event);
		return;
	}

	if (!event->contact) {
		return;
	}

	decrease_event_count (window, event);

	data.on = FALSE;
	data.event = event;
	data.window = window;
	gtk_tree_model_foreach (GTK_TREE_MODEL (priv->individual_store),
				main_window_flash_foreach,
				&data);
}

static gboolean
main_window_load_events_idle_cb (gpointer user_data)
{
	EmpathyMainWindow *window = user_data;
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GSList *l;

	l = empathy_event_manager_get_events (priv->event_manager);
	while (l) {
		main_window_event_added_cb (priv->event_manager, l->data,
				window);
		l = l->next;
	}

	return FALSE;
}

static void
main_window_row_activated_cb (EmpathyIndividualView *view,
			      GtkTreePath            *path,
			      GtkTreeViewColumn      *col,
			      EmpathyMainWindow      *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	EmpathyContact *contact = NULL;
	FolksIndividual *individual;
	GtkTreeModel   *model;
	GtkTreeIter     iter;
	GSList         *events, *l;

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->individual_view));
	gtk_tree_model_get_iter (model, &iter, path);

	gtk_tree_model_get (model, &iter,
			    EMPATHY_INDIVIDUAL_STORE_COL_INDIVIDUAL,
				&individual,
			    -1);

	if (individual != NULL) {
		contact = empathy_contact_dup_from_folks_individual (individual);
	}

	if (!contact) {
		goto OUT;
	}

	/* If the contact has an event activate it, otherwise the
	 * default handler of row-activated will be called. */
	events = empathy_event_manager_get_events (priv->event_manager);
	for (l = events; l; l = l->next) {
		EmpathyEvent *event = l->data;

		if (event->contact == contact) {
			DEBUG ("Activate event");
			empathy_event_activate (event);

			/* We don't want the default handler of this signal
			 * (e.g. open a chat) */
			g_signal_stop_emission_by_name (view, "row-activated");
			break;
		}
	}

	g_object_unref (contact);
OUT:
	tp_clear_object (&individual);
}

static void
main_window_row_deleted_cb (GtkTreeModel      *model,
			    GtkTreePath       *path,
			    EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GtkTreeIter help_iter;

	if (!gtk_tree_model_get_iter_first (model, &help_iter)) {
		priv->empty = TRUE;

		if (empathy_individual_view_is_searching (
				priv->individual_view)) {
			gchar *tmp;

			tmp = g_strdup_printf ("<b><span size='xx-large'>%s</span></b>",
				_("No match found"));

			gtk_label_set_markup (GTK_LABEL (priv->no_entry_label), tmp);
			g_free (tmp);

			gtk_label_set_line_wrap (GTK_LABEL (priv->no_entry_label),
				TRUE);

			gtk_notebook_set_current_page (
					GTK_NOTEBOOK (priv->notebook), PAGE_NO_MATCH);
		}
	}
}

static void
main_window_row_inserted_cb (GtkTreeModel      *model,
			     GtkTreePath       *path,
			     GtkTreeIter       *iter,
			     EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);

	if (priv->empty) {
		priv->empty = FALSE;
		gtk_notebook_set_current_page (GTK_NOTEBOOK (priv->notebook),
				PAGE_CONTACT_LIST);
		gtk_widget_grab_focus (GTK_WIDGET (priv->individual_view));

		/* The store is being filled, it will be done after an idle cb.
		 * So we can then get events. If we do that too soon, event's
		 * contact is not yet in the store and it won't get marked as
		 * having events. */
		g_idle_add (main_window_load_events_idle_cb, window);
	}
}

static void
main_window_remove_error (EmpathyMainWindow *window,
			  TpAccount         *account)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GtkWidget *error_widget;

	error_widget = g_hash_table_lookup (priv->errors, account);
	if (error_widget != NULL) {
		gtk_widget_destroy (error_widget);
		g_hash_table_remove (priv->errors, account);
	}
}

static void
main_window_account_disabled_cb (TpAccountManager  *manager,
				 TpAccount         *account,
				 EmpathyMainWindow *window)
{
	main_window_remove_error (window, account);
}

static void
main_window_error_retry_clicked_cb (GtkButton         *button,
				    EmpathyMainWindow *window)
{
	TpAccount *account;

	account = g_object_get_data (G_OBJECT (button), "account");
	tp_account_reconnect_async (account, NULL, NULL);

	main_window_remove_error (window, account);
}

static void
main_window_error_edit_clicked_cb (GtkButton         *button,
				   EmpathyMainWindow *window)
{
	TpAccount *account;

	account = g_object_get_data (G_OBJECT (button), "account");

	empathy_accounts_dialog_show_application (
			gtk_widget_get_screen (GTK_WIDGET (button)),
			account, FALSE, FALSE);

	main_window_remove_error (window, account);
}

static void
main_window_error_close_clicked_cb (GtkButton         *button,
				    EmpathyMainWindow *window)
{
	TpAccount *account;

	account = g_object_get_data (G_OBJECT (button), "account");
	main_window_remove_error (window, account);
}

static void
main_window_error_upgrade_sw_clicked_cb (GtkButton         *button,
					 EmpathyMainWindow *window)
{
	TpAccount *account;
	GtkWidget *dialog;

	account = g_object_get_data (G_OBJECT (button), "account");
	main_window_remove_error (window, account);

	dialog = gtk_message_dialog_new (GTK_WINDOW (window),
		GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
		GTK_BUTTONS_OK,
		_("Sorry, %s accounts can’t be used until your %s software is updated."),
		tp_account_get_protocol (account),
		tp_account_get_protocol (account));

	g_signal_connect_swapped (dialog, "response",
		G_CALLBACK (gtk_widget_destroy),
		dialog);

	gtk_widget_show (dialog);
}

static void
main_window_upgrade_software_error (EmpathyMainWindow *window,
				    TpAccount         *account)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GtkWidget *info_bar;
	GtkWidget *content_area;
	GtkWidget *label;
	GtkWidget *image;
	GtkWidget *upgrade_button;
	GtkWidget *close_button;
	GtkWidget *action_area;
	GtkWidget *action_grid;
	gchar     *str;
	const gchar     *icon_name;
	const gchar *error_message;
	gboolean user_requested;

	error_message =
		empathy_account_get_error_message (account, &user_requested);

	if (user_requested) {
		return;
	}

	str = g_markup_printf_escaped ("<b>%s</b>\n%s",
					       tp_account_get_display_name (account),
					       error_message);

	/* If there are other errors, remove them */
	main_window_remove_error (window, account);

	info_bar = gtk_info_bar_new ();
	gtk_info_bar_set_message_type (GTK_INFO_BAR (info_bar), GTK_MESSAGE_ERROR);

	gtk_widget_set_no_show_all (info_bar, TRUE);
	gtk_box_pack_start (GTK_BOX (priv->errors_vbox), info_bar, FALSE, TRUE, 0);
	gtk_widget_show (info_bar);

	icon_name = tp_account_get_icon_name (account);
	image = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_widget_show (image);

	label = gtk_label_new (str);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_widget_show (label);
	g_free (str);

	content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (info_bar));
	gtk_box_pack_start (GTK_BOX (content_area), image, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (content_area), label, TRUE, TRUE, 0);

	image = gtk_image_new_from_stock (GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON);
	upgrade_button = gtk_button_new ();
	gtk_button_set_image (GTK_BUTTON (upgrade_button), image);
	gtk_widget_set_tooltip_text (upgrade_button, _("Update software..."));
	gtk_widget_show (upgrade_button);

	image = gtk_image_new_from_stock (GTK_STOCK_CLOSE, GTK_ICON_SIZE_BUTTON);
	close_button = gtk_button_new ();
	gtk_button_set_image (GTK_BUTTON (close_button), image);
	gtk_widget_set_tooltip_text (close_button, _("Close"));
	gtk_widget_show (close_button);

	action_grid = gtk_grid_new ();
	gtk_grid_set_column_spacing (GTK_GRID (action_grid), 2);
	gtk_widget_show (action_grid);

	action_area = gtk_info_bar_get_action_area (GTK_INFO_BAR (info_bar));
	gtk_box_pack_start (GTK_BOX (action_area), action_grid, FALSE, FALSE, 0);

	gtk_grid_attach (GTK_GRID (action_grid), upgrade_button, 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (action_grid), close_button, 1, 0, 1, 1);

	g_object_set_data (G_OBJECT (info_bar), "label", label);
	g_object_set_data_full (G_OBJECT (info_bar),
				"account", g_object_ref (account),
				g_object_unref);
	g_object_set_data_full (G_OBJECT (upgrade_button),
				"account", g_object_ref (account),
				g_object_unref);
	g_object_set_data_full (G_OBJECT (close_button),
				"account", g_object_ref (account),
				g_object_unref);

	g_signal_connect (upgrade_button, "clicked",
			  G_CALLBACK (main_window_error_upgrade_sw_clicked_cb),
			  window);
	g_signal_connect (close_button, "clicked",
			  G_CALLBACK (main_window_error_close_clicked_cb),
			  window);

	gtk_widget_set_tooltip_text (priv->errors_vbox, error_message);
	gtk_widget_show (priv->errors_vbox);

	g_hash_table_insert (priv->errors, g_object_ref (account), info_bar);
}

static void
main_window_error_display (EmpathyMainWindow *window,
			   TpAccount         *account)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GtkWidget *info_bar;
	GtkWidget *content_area;
	GtkWidget *label;
	GtkWidget *image;
	GtkWidget *retry_button;
	GtkWidget *edit_button;
	GtkWidget *close_button;
	GtkWidget *action_area;
	GtkWidget *action_grid;
	gchar     *str;
	const gchar     *icon_name;
	const gchar *error_message;
	gboolean user_requested;

	if (!tp_strdiff (TP_ERROR_STR_SOFTWARE_UPGRADE_REQUIRED,
			 tp_account_get_detailed_error (account, NULL))) {
		main_window_upgrade_software_error (window, account);
		return;
	}

	error_message =
		empathy_account_get_error_message (account, &user_requested);

	if (user_requested) {
		return;
	}

	str = g_markup_printf_escaped ("<b>%s</b>\n%s",
					       tp_account_get_display_name (account),
					       error_message);

	info_bar = g_hash_table_lookup (priv->errors, account);
	if (info_bar) {
		label = g_object_get_data (G_OBJECT (info_bar), "label");

		/* Just set the latest error and return */
		gtk_label_set_markup (GTK_LABEL (label), str);
		g_free (str);

		return;
	}

	info_bar = gtk_info_bar_new ();
	gtk_info_bar_set_message_type (GTK_INFO_BAR (info_bar), GTK_MESSAGE_ERROR);

	gtk_widget_set_no_show_all (info_bar, TRUE);
	gtk_box_pack_start (GTK_BOX (priv->errors_vbox), info_bar, FALSE, TRUE, 0);
	gtk_widget_show (info_bar);

	icon_name = tp_account_get_icon_name (account);
	image = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_widget_show (image);

	label = gtk_label_new (str);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_widget_show (label);
	g_free (str);

	content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (info_bar));
	gtk_box_pack_start (GTK_BOX (content_area), image, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (content_area), label, TRUE, TRUE, 0);

	image = gtk_image_new_from_stock (GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON);
	retry_button = gtk_button_new ();
	gtk_button_set_image (GTK_BUTTON (retry_button), image);
	gtk_widget_set_tooltip_text (retry_button, _("Reconnect"));
	gtk_widget_show (retry_button);

	image = gtk_image_new_from_stock (GTK_STOCK_EDIT, GTK_ICON_SIZE_BUTTON);
	edit_button = gtk_button_new ();
	gtk_button_set_image (GTK_BUTTON (edit_button), image);
	gtk_widget_set_tooltip_text (edit_button, _("Edit Account"));
	gtk_widget_show (edit_button);

	image = gtk_image_new_from_stock (GTK_STOCK_CLOSE, GTK_ICON_SIZE_BUTTON);
	close_button = gtk_button_new ();
	gtk_button_set_image (GTK_BUTTON (close_button), image);
	gtk_widget_set_tooltip_text (close_button, _("Close"));
	gtk_widget_show (close_button);

	action_grid = gtk_grid_new ();
	gtk_grid_set_column_spacing (GTK_GRID (action_grid), 2);
	gtk_widget_show (action_grid);

	action_area = gtk_info_bar_get_action_area (GTK_INFO_BAR (info_bar));
	gtk_box_pack_start (GTK_BOX (action_area), action_grid, FALSE, FALSE, 0);

	gtk_grid_attach (GTK_GRID (action_grid), retry_button, 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (action_grid), edit_button, 1, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (action_grid), close_button, 2, 0, 1, 1);

	g_object_set_data (G_OBJECT (info_bar), "label", label);
	g_object_set_data_full (G_OBJECT (info_bar),
				"account", g_object_ref (account),
				g_object_unref);
	g_object_set_data_full (G_OBJECT (edit_button),
				"account", g_object_ref (account),
				g_object_unref);
	g_object_set_data_full (G_OBJECT (close_button),
				"account", g_object_ref (account),
				g_object_unref);
	g_object_set_data_full (G_OBJECT (retry_button),
				"account", g_object_ref (account),
				g_object_unref);

	g_signal_connect (edit_button, "clicked",
			  G_CALLBACK (main_window_error_edit_clicked_cb),
			  window);
	g_signal_connect (close_button, "clicked",
			  G_CALLBACK (main_window_error_close_clicked_cb),
			  window);
	g_signal_connect (retry_button, "clicked",
			  G_CALLBACK (main_window_error_retry_clicked_cb),
			  window);

	gtk_widget_set_tooltip_text (priv->errors_vbox, error_message);
	gtk_widget_show (priv->errors_vbox);

	g_hash_table_insert (priv->errors, g_object_ref (account), info_bar);
}

static void
main_window_update_status (EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	gboolean connected, connecting;
	GList *l, *children;

	connected = empathy_account_manager_get_accounts_connected (&connecting);

	/* Update the spinner state */
	if (connecting) {
		gtk_spinner_start (GTK_SPINNER (priv->throbber));
		gtk_widget_show (priv->throbber_tool_item);
	} else {
		gtk_spinner_stop (GTK_SPINNER (priv->throbber));
		gtk_widget_hide (priv->throbber_tool_item);
	}

	/* Update widgets sensibility */
	for (l = priv->actions_connected; l; l = l->next) {
		gtk_action_set_sensitive (l->data, connected);
	}

	/* Update favourite rooms sensitivity */
	children = gtk_container_get_children (GTK_CONTAINER (priv->room_menu));
	for (l = children; l != NULL; l = l->next) {
		if (g_object_get_data (G_OBJECT (l->data), "is_favorite") != NULL) {
			gtk_widget_set_sensitive (GTK_WIDGET (l->data), connected);
		}
	}
	g_list_free (children);
}

static char *
main_window_account_to_action_name (TpAccount *account)
{
	char *r;

	/* action names can't have '/' in them, replace it with '.' */
	r = g_strdup (tp_account_get_path_suffix (account));
	r = g_strdelimit (r, "/", '.');

	return r;
}

static void
main_window_balance_activate_cb (GtkAction         *action,
				 EmpathyMainWindow *window)
{
	const char *uri;

	uri = g_object_get_data (G_OBJECT (action), "manage-credit-uri");

	if (!tp_str_empty (uri)) {
		DEBUG ("Top-up credit URI: %s", uri);
		empathy_url_show (GTK_WIDGET (window), uri);
	} else {
		DEBUG ("unknown protocol for top-up");
	}
}

static void
main_window_balance_update_balance (GtkAction   *action,
				    TpConnection *conn)
{
	TpAccount *account = tp_connection_get_account (conn);
	GtkWidget *label;
	int amount = 0;
	guint scale = G_MAXINT32;
	const gchar *currency = "";
	char *money, *str;

	if (!tp_connection_get_balance (conn, &amount, &scale, &currency))
		return;

	if (amount == 0 &&
	    scale == G_MAXINT32 &&
	    tp_str_empty (currency)) {
		/* unknown balance */
		money = g_strdup ("--");
	} else {
		char *tmp = empathy_format_currency (amount, scale, currency);

		money = g_strdup_printf ("%s %s", currency, tmp);
		g_free (tmp);
	}

	/* Translators: this string will be something like:
	 *   Top up My Account ($1.23)..." */
	str = g_strdup_printf (_("Top up %s (%s)..."),
		tp_account_get_display_name (account),
		money);

	gtk_action_set_label (action, str);
	g_free (str);

	/* update the money label in the roster */
	label = g_object_get_data (G_OBJECT (action), "money-label");

	gtk_label_set_text (GTK_LABEL (label), money);
	g_free (money);
}

static void
main_window_balance_changed_cb (TpConnection      *conn,
				guint balance,
				guint scale,
				const gchar *currency,
				GtkAction *action)
{
	main_window_balance_update_balance (action, conn);
}

static GtkAction *
main_window_setup_balance_create_action (EmpathyMainWindow *window,
					 TpAccount *account)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GtkAction *action;
	char *name, *ui;
	guint merge_id;
	GError *error = NULL;

	/* create the action group if required */
	if (priv->balance_action_group == NULL) {
		priv->balance_action_group =
			gtk_action_group_new ("balance-action-group");

		gtk_ui_manager_insert_action_group (priv->ui_manager,
			priv->balance_action_group, -1);
	}

	/* create the action */
	name = main_window_account_to_action_name (account);
	action = gtk_action_new (name,
		tp_account_get_display_name (account),
		_("Top up account credit"),
		NULL);
	g_object_bind_property (account, "icon-name", action, "icon-name",
		G_BINDING_SYNC_CREATE);

	g_signal_connect (action, "activate",
		G_CALLBACK (main_window_balance_activate_cb), window);

	gtk_action_group_add_action (priv->balance_action_group, action);
	g_object_unref (action);

	ui = g_strdup_printf (
		"<ui>"
		" <menubar name='menubar'>"
		"  <menu action='view'>"
		"   <placeholder name='view_balance_placeholder'>"
		"    <menuitem action='%s'/>"
		"   </placeholder>"
		"  </menu>"
		" </menubar>"
		"</ui>",
		name);

	merge_id = gtk_ui_manager_add_ui_from_string (priv->ui_manager,
		ui, -1, &error);
	if (error != NULL) {
		DEBUG ("Failed to add balance UI for %s: %s",
			tp_account_get_display_name (account),
			error->message);
		g_error_free (error);
	}

	g_object_set_data (G_OBJECT (action),
		"merge-id", GUINT_TO_POINTER (merge_id));

	g_free (name);
	g_free (ui);

	return action;
}

static GtkWidget *
main_window_setup_balance_create_widget (EmpathyMainWindow *window,
					 GtkAction         *action,
					 TpAccount *account)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GtkWidget *hbox, *image, *label, *button;

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

	/* protocol icon */
	image = gtk_image_new ();
	gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);
	g_object_bind_property (action, "icon-name", image, "icon-name",
		G_BINDING_SYNC_CREATE);

	/* account name label */
	label = gtk_label_new ("");
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
	g_object_bind_property (account, "display-name", label, "label",
		G_BINDING_SYNC_CREATE);

	/* balance label */
	label = gtk_label_new ("");
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
	g_object_set_data (G_OBJECT (action), "money-label", label);

	/* top up button */
	button = gtk_button_new_with_label (_("Top Up..."));
	gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, TRUE, 0);
	g_signal_connect_swapped (button, "clicked",
		G_CALLBACK (gtk_action_activate), action);

	gtk_box_pack_start (GTK_BOX (priv->balance_vbox), hbox, FALSE, TRUE, 0);
	gtk_widget_show_all (hbox);

	/* tie the lifetime of the widget to the lifetime of the action */
	g_object_weak_ref (G_OBJECT (action),
		(GWeakNotify) gtk_widget_destroy, hbox);

	return hbox;
}

static void
main_window_setup_balance (EmpathyMainWindow *window,
			   TpAccount         *account)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	TpConnection *conn = tp_account_get_connection (account);
	GtkAction *action;
	const gchar *uri;

	if (conn == NULL)
		return;

	if (!tp_proxy_is_prepared (conn, TP_CONNECTION_FEATURE_BALANCE))
		return;

	DEBUG ("Setting up balance for acct: %s",
		tp_account_get_display_name (account));

	/* create the action */
	action = main_window_setup_balance_create_action (window, account);

	if (action == NULL)
		return;

	gtk_action_set_visible (priv->view_balance_show_in_roster, TRUE);

	/* create the display widget */
	main_window_setup_balance_create_widget (window, action, account);

	/* check the current balance and monitor for any changes */
	uri = tp_connection_get_balance_uri (conn);

	g_object_set_data_full (G_OBJECT (action), "manage-credit-uri",
		g_strdup (uri), g_free);
	gtk_action_set_sensitive (GTK_ACTION (action), !tp_str_empty (uri));

	main_window_balance_update_balance (GTK_ACTION (action), conn);

	g_signal_connect (conn, "balance-changed",
		G_CALLBACK (main_window_balance_changed_cb), action);

}

static void
main_window_remove_balance_action (EmpathyMainWindow *window,
				   TpAccount         *account)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GtkAction *action;
	char *name;
	GList *a;

	if (priv->balance_action_group == NULL)
		return;

	name = main_window_account_to_action_name (account);

	action = gtk_action_group_get_action (
		priv->balance_action_group, name);

	if (action != NULL) {
		guint merge_id;

		DEBUG ("Removing action");

		merge_id = GPOINTER_TO_UINT (g_object_get_data (
			G_OBJECT (action),
			"merge-id"));

		gtk_ui_manager_remove_ui (priv->ui_manager,
			merge_id);
		gtk_action_group_remove_action (
			priv->balance_action_group, action);
	}

	g_free (name);

	a = gtk_action_group_list_actions (
		priv->balance_action_group);

	gtk_action_set_visible (
		priv->view_balance_show_in_roster,
		g_list_length (a) > 0);

	g_list_free (a);
}

static void
main_window_connection_changed_cb (TpAccount  *account,
				   guint       old_status,
				   guint       current,
				   guint       reason,
				   gchar      *dbus_error_name,
				   GHashTable *details,
				   EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);

	main_window_update_status (window);

	if (current == TP_CONNECTION_STATUS_DISCONNECTED &&
	    reason != TP_CONNECTION_STATUS_REASON_REQUESTED) {
		main_window_error_display (window, account);
	}

	if (current == TP_CONNECTION_STATUS_DISCONNECTED) {
		empathy_sound_manager_play (priv->sound_mgr, GTK_WIDGET (window),
				    EMPATHY_SOUND_ACCOUNT_DISCONNECTED);

		/* remove balance action if required */
		main_window_remove_balance_action (window, account);
	}

	if (current == TP_CONNECTION_STATUS_CONNECTED) {
		empathy_sound_manager_play (priv->sound_mgr, GTK_WIDGET (window),
				    EMPATHY_SOUND_ACCOUNT_CONNECTED);

		/* Account connected without error, remove error message if any */
		main_window_remove_error (window, account);
		main_window_setup_balance (window, account);
	}
}

static void
main_window_accels_load (void)
{
	gchar *filename;

	filename = g_build_filename (g_get_user_config_dir (), PACKAGE_NAME, ACCELS_FILENAME, NULL);
	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		DEBUG ("Loading from:'%s'", filename);
		gtk_accel_map_load (filename);
	}

	g_free (filename);
}

static void
main_window_accels_save (void)
{
	gchar *dir;
	gchar *file_with_path;

	dir = g_build_filename (g_get_user_config_dir (), PACKAGE_NAME, NULL);
	g_mkdir_with_parents (dir, S_IRUSR | S_IWUSR | S_IXUSR);
	file_with_path = g_build_filename (dir, ACCELS_FILENAME, NULL);
	g_free (dir);

	DEBUG ("Saving to:'%s'", file_with_path);
	gtk_accel_map_save (file_with_path);

	g_free (file_with_path);
}

static void
empathy_main_window_finalize (GObject *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GHashTableIter iter;
	gpointer key, value;

	/* Save user-defined accelerators. */
	main_window_accels_save ();

	g_list_free (priv->actions_connected);

	g_object_unref (priv->account_manager);
	g_object_unref (priv->individual_store);
	g_object_unref (priv->contact_manager);
	g_object_unref (priv->sound_mgr);
	g_hash_table_unref (priv->errors);
	g_hash_table_unref (priv->auths);

	/* disconnect all handlers of status-changed signal */
	g_hash_table_iter_init (&iter, priv->status_changed_handlers);
	while (g_hash_table_iter_next (&iter, &key, &value))
		g_signal_handler_disconnect (TP_ACCOUNT (key),
					     GPOINTER_TO_UINT (value));

	g_hash_table_unref (priv->status_changed_handlers);

	g_signal_handlers_disconnect_by_func (priv->event_manager,
			  		      main_window_event_added_cb,
			  		      window);
	g_signal_handlers_disconnect_by_func (priv->event_manager,
			  		      main_window_event_removed_cb,
			  		      window);
	g_object_unref (priv->call_observer);
	g_object_unref (priv->event_manager);
	g_object_unref (priv->ui_manager);
	g_object_unref (priv->chatroom_manager);

	g_object_unref (priv->gsettings_ui);
	g_object_unref (priv->gsettings_contacts);

	G_OBJECT_CLASS (empathy_main_window_parent_class)->finalize (window);
}

static gboolean
main_window_key_press_event_cb  (GtkWidget   *window,
				 GdkEventKey *event,
				 gpointer     user_data)
{
	if (event->keyval == GDK_KEY_T
	    && event->state & GDK_SHIFT_MASK
	    && event->state & GDK_CONTROL_MASK) {
		empathy_chat_manager_call_undo_closed_chat ();
	}
	return FALSE;
}

static void
main_window_chat_quit_cb (GtkAction         *action,
			  EmpathyMainWindow *window)
{
	gtk_widget_destroy (GTK_WIDGET (window));
}

static void
main_window_view_history_cb (GtkAction         *action,
			     EmpathyMainWindow *window)
{
	empathy_log_window_show (NULL, NULL, FALSE, GTK_WINDOW (window));
}

static void
main_window_chat_new_message_cb (GtkAction         *action,
				 EmpathyMainWindow *window)
{
	empathy_new_message_dialog_show (GTK_WINDOW (window));
}

static void
main_window_chat_new_call_cb (GtkAction         *action,
			      EmpathyMainWindow *window)
{
	empathy_new_call_dialog_show (GTK_WINDOW (window));
}

static void
main_window_chat_add_contact_cb (GtkAction         *action,
				 EmpathyMainWindow *window)
{
	empathy_new_individual_dialog_show (GTK_WINDOW (window));
}

static void
main_window_chat_search_contacts_cb (GtkAction         *action,
				     EmpathyMainWindow *window)
{
	GtkWidget *dialog = empathy_contact_search_dialog_new (
			GTK_WINDOW (window));
	gtk_widget_show (dialog);
}

static void
main_window_view_show_ft_manager (GtkAction         *action,
				  EmpathyMainWindow *window)
{
	empathy_ft_manager_show ();
}

static void
main_window_view_show_offline_cb (GtkToggleAction   *action,
				  EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	gboolean current;

	current = gtk_toggle_action_get_active (action);
	g_settings_set_boolean (priv->gsettings_ui,
				EMPATHY_PREFS_UI_SHOW_OFFLINE,
				current);

	empathy_individual_view_set_show_offline (priv->individual_view,
			current);
}

static void
main_window_notify_sort_contact_cb (GSettings         *gsettings,
				    const gchar       *key,
				    EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	gchar *str;

	str = g_settings_get_string (gsettings, key);

	if (str != NULL) {
		GType       type;
		GEnumClass *enum_class;
		GEnumValue *enum_value;

		type = empathy_individual_store_sort_get_type ();
		enum_class = G_ENUM_CLASS (g_type_class_peek (type));
		enum_value = g_enum_get_value_by_nick (enum_class, str);
		if (enum_value) {
			/* By changing the value of the GtkRadioAction,
			   it emits a signal that calls main_window_view_sort_contacts_cb
			   which updates the contacts list */
			gtk_radio_action_set_current_value (priv->sort_by_name,
							    enum_value->value);
		} else {
			g_warning ("Wrong value for sort_criterium configuration : %s", str);
		}
		g_free (str);
	}
}

static void
main_window_view_sort_contacts_cb (GtkRadioAction    *action,
				   GtkRadioAction    *current,
				   EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	EmpathyIndividualStoreSort value;
	GSList      *group;
	GType        type;
	GEnumClass  *enum_class;
	GEnumValue  *enum_value;

	value = gtk_radio_action_get_current_value (action);
	group = gtk_radio_action_get_group (action);

	/* Get string from index */
	type = empathy_individual_store_sort_get_type ();
	enum_class = G_ENUM_CLASS (g_type_class_peek (type));
	enum_value = g_enum_get_value (enum_class, g_slist_index (group, current));

	if (!enum_value) {
		g_warning ("No GEnumValue for EmpathyContactListSort with GtkRadioAction index:%d",
			   g_slist_index (group, action));
	} else {
		g_settings_set_string (priv->gsettings_contacts,
				       EMPATHY_PREFS_CONTACTS_SORT_CRITERIUM,
				       enum_value->value_nick);
	}
	empathy_individual_store_set_sort_criterium (priv->individual_store,
			value);
}

static void
main_window_view_show_protocols_cb (GtkToggleAction   *action,
				    EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	gboolean value;

	value = gtk_toggle_action_get_active (action);

	g_settings_set_boolean (priv->gsettings_ui,
				EMPATHY_PREFS_UI_SHOW_PROTOCOLS,
				value);
	empathy_individual_store_set_show_protocols (priv->individual_store,
						     value);
}

/* Matches GtkRadioAction values set in empathy-main-window.ui */
#define CONTACT_LIST_NORMAL_SIZE_WITH_AVATARS		0
#define CONTACT_LIST_NORMAL_SIZE			1
#define CONTACT_LIST_COMPACT_SIZE			2

static void
main_window_view_contacts_list_size_cb (GtkRadioAction    *action,
					GtkRadioAction    *current,
					EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GSettings *gsettings_ui;
	gint value;

	value = gtk_radio_action_get_current_value (action);
	/* create a new GSettings, so we can delay the setting until both
	 * values are set */
	gsettings_ui = g_settings_new (EMPATHY_PREFS_UI_SCHEMA);

	DEBUG ("radio button toggled, value = %i", value);

	g_settings_delay (gsettings_ui);
	g_settings_set_boolean (gsettings_ui,
				EMPATHY_PREFS_UI_SHOW_AVATARS,
				value == CONTACT_LIST_NORMAL_SIZE_WITH_AVATARS);

	g_settings_set_boolean (gsettings_ui,
				EMPATHY_PREFS_UI_COMPACT_CONTACT_LIST,
				value == CONTACT_LIST_COMPACT_SIZE);
	g_settings_apply (gsettings_ui);

	/* FIXME: these enums probably have the wrong namespace */
	empathy_individual_store_set_show_avatars (priv->individual_store,
			value == CONTACT_LIST_NORMAL_SIZE_WITH_AVATARS);
	empathy_individual_store_set_is_compact (priv->individual_store,
			value == CONTACT_LIST_COMPACT_SIZE);

	g_object_unref (gsettings_ui);
}

static void main_window_notify_show_protocols_cb (GSettings         *gsettings,
						  const gchar       *key,
						  EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);

	gtk_toggle_action_set_active (priv->show_protocols,
			g_settings_get_boolean (gsettings,
				EMPATHY_PREFS_UI_SHOW_PROTOCOLS));
}


static void
main_window_notify_contact_list_size_cb (GSettings         *gsettings,
					 const gchar       *key,
					 EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	gint value;

	if (g_settings_get_boolean (gsettings,
			EMPATHY_PREFS_UI_COMPACT_CONTACT_LIST)) {
		value = CONTACT_LIST_COMPACT_SIZE;
	} else if (g_settings_get_boolean (gsettings,
			EMPATHY_PREFS_UI_SHOW_AVATARS)) {
		value = CONTACT_LIST_NORMAL_SIZE_WITH_AVATARS;
	} else {
		value = CONTACT_LIST_NORMAL_SIZE;
	}

	DEBUG ("setting changed, value = %i", value);

	/* By changing the value of the GtkRadioAction,
	   it emits a signal that calls main_window_view_contacts_list_size_cb
	   which updates the contacts list */
	gtk_radio_action_set_current_value (priv->normal_with_avatars, value);
}

static void
main_window_edit_search_contacts_cb (GtkCheckMenuItem  *item,
				     EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);

	empathy_individual_view_start_search (priv->individual_view);
}

static void
main_window_view_show_map_cb (GtkCheckMenuItem  *item,
			      EmpathyMainWindow *window)
{
#ifdef HAVE_LIBCHAMPLAIN
	empathy_map_view_show ();
#endif
}

static void
join_chatroom (EmpathyChatroom *chatroom,
	       gint64 timestamp)
{
	TpAccount      *account;
	const gchar    *room;

	account = empathy_chatroom_get_account (chatroom);
	room = empathy_chatroom_get_room (chatroom);

	DEBUG ("Requesting channel for '%s'", room);
	empathy_join_muc (account, room, timestamp);
}

typedef struct
{
	TpAccount *account;
	EmpathyChatroom *chatroom;
	gint64 timestamp;
	glong sig_id;
	guint timeout;
} join_fav_account_sig_ctx;

static join_fav_account_sig_ctx *
join_fav_account_sig_ctx_new (TpAccount *account,
			     EmpathyChatroom *chatroom,
			      gint64 timestamp)
{
	join_fav_account_sig_ctx *ctx = g_slice_new0 (
		join_fav_account_sig_ctx);

	ctx->account = g_object_ref (account);
	ctx->chatroom = g_object_ref (chatroom);
	ctx->timestamp = timestamp;
	return ctx;
}

static void
join_fav_account_sig_ctx_free (join_fav_account_sig_ctx *ctx)
{
	g_object_unref (ctx->account);
	g_object_unref (ctx->chatroom);
	g_slice_free (join_fav_account_sig_ctx, ctx);
}

static void
account_status_changed_cb (TpAccount  *account,
			   TpConnectionStatus old_status,
			   TpConnectionStatus new_status,
			   guint reason,
			   gchar *dbus_error_name,
			   GHashTable *details,
			   gpointer user_data)
{
	join_fav_account_sig_ctx *ctx = user_data;

	switch (new_status) {
		case TP_CONNECTION_STATUS_DISCONNECTED:
			/* Don't wait any longer */
			goto finally;
			break;

		case TP_CONNECTION_STATUS_CONNECTING:
			/* Wait a bit */
			return;

		case TP_CONNECTION_STATUS_CONNECTED:
			/* We can join the room */
			break;

		default:
			g_assert_not_reached ();
	}

	join_chatroom (ctx->chatroom, ctx->timestamp);

finally:
	g_source_remove (ctx->timeout);
	g_signal_handler_disconnect (account, ctx->sig_id);
}

#define JOIN_FAVORITE_TIMEOUT 5

static gboolean
join_favorite_timeout_cb (gpointer data)
{
	join_fav_account_sig_ctx *ctx = data;

	/* stop waiting for joining the favorite room */
	g_signal_handler_disconnect (ctx->account, ctx->sig_id);
	return FALSE;
}

static void
main_window_favorite_chatroom_join (EmpathyChatroom *chatroom)
{
	TpAccount      *account;

	account = empathy_chatroom_get_account (chatroom);
	if (tp_account_get_connection_status (account, NULL) !=
					     TP_CONNECTION_STATUS_CONNECTED) {
		join_fav_account_sig_ctx *ctx;

		ctx = join_fav_account_sig_ctx_new (account, chatroom,
			empathy_get_current_action_time ());

		ctx->sig_id = g_signal_connect_data (account, "status-changed",
			G_CALLBACK (account_status_changed_cb), ctx,
			(GClosureNotify) join_fav_account_sig_ctx_free, 0);

		ctx->timeout = g_timeout_add_seconds (JOIN_FAVORITE_TIMEOUT,
			join_favorite_timeout_cb, ctx);
		return;
	}

	join_chatroom (chatroom, empathy_get_current_action_time ());
}

static void
main_window_favorite_chatroom_menu_activate_cb (GtkMenuItem     *menu_item,
						EmpathyChatroom *chatroom)
{
	main_window_favorite_chatroom_join (chatroom);
}

static void
main_window_favorite_chatroom_menu_add (EmpathyMainWindow *window,
					EmpathyChatroom   *chatroom)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GtkWidget   *menu_item;
	const gchar *name, *account_name;
	gchar *label;


	if (g_object_get_data (G_OBJECT (chatroom), "menu_item")) {
		return;
	}

	name = empathy_chatroom_get_name (chatroom);
	account_name = tp_account_get_display_name (
			empathy_chatroom_get_account (chatroom));
	label = g_strdup_printf ("%s (%s)", name, account_name);
	menu_item = gtk_menu_item_new_with_label (label);
	g_free (label);
	g_object_set_data (G_OBJECT (menu_item), "is_favorite",
			GUINT_TO_POINTER (TRUE));

	g_object_set_data (G_OBJECT (chatroom), "menu_item", menu_item);
	g_signal_connect (menu_item, "activate",
			  G_CALLBACK (main_window_favorite_chatroom_menu_activate_cb),
			  chatroom);

	gtk_menu_shell_insert (GTK_MENU_SHELL (priv->room_menu),
			       menu_item, 4);

	gtk_widget_show (menu_item);
}

static void
main_window_favorite_chatroom_menu_added_cb (EmpathyChatroomManager *manager,
					     EmpathyChatroom        *chatroom,
					     EmpathyMainWindow      *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);

	main_window_favorite_chatroom_menu_add (window, chatroom);
	gtk_widget_show (priv->room_separator);
	gtk_action_set_sensitive (priv->room_join_favorites, TRUE);
}

static void
main_window_favorite_chatroom_menu_removed_cb (EmpathyChatroomManager *manager,
					       EmpathyChatroom        *chatroom,
					       EmpathyMainWindow      *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GtkWidget *menu_item;
	GList *chatrooms;

	menu_item = g_object_get_data (G_OBJECT (chatroom), "menu_item");
	g_object_set_data (G_OBJECT (chatroom), "menu_item", NULL);
	gtk_widget_destroy (menu_item);

	chatrooms = empathy_chatroom_manager_get_chatrooms (priv->chatroom_manager, NULL);
	if (chatrooms) {
		gtk_widget_show (priv->room_separator);
	} else {
		gtk_widget_hide (priv->room_separator);
	}

	gtk_action_set_sensitive (priv->room_join_favorites, chatrooms != NULL);
	g_list_free (chatrooms);
}

static void
main_window_favorite_chatroom_menu_setup (EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GList *chatrooms, *l;
	GtkWidget *room;

	priv->chatroom_manager = empathy_chatroom_manager_dup_singleton (NULL);
	chatrooms = empathy_chatroom_manager_get_chatrooms (
		priv->chatroom_manager, NULL);
	room = gtk_ui_manager_get_widget (priv->ui_manager,
		"/menubar/room");
	priv->room_menu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (room));
	priv->room_separator = gtk_ui_manager_get_widget (priv->ui_manager,
		"/menubar/room/room_separator");

	for (l = chatrooms; l; l = l->next) {
		main_window_favorite_chatroom_menu_add (window, l->data);
	}

	if (!chatrooms) {
		gtk_widget_hide (priv->room_separator);
	}

	gtk_action_set_sensitive (priv->room_join_favorites, chatrooms != NULL);

	g_signal_connect (priv->chatroom_manager, "chatroom-added",
			  G_CALLBACK (main_window_favorite_chatroom_menu_added_cb),
			  window);
	g_signal_connect (priv->chatroom_manager, "chatroom-removed",
			  G_CALLBACK (main_window_favorite_chatroom_menu_removed_cb),
			  window);

	g_list_free (chatrooms);
}

static void
main_window_room_join_new_cb (GtkAction         *action,
			      EmpathyMainWindow *window)
{
	empathy_new_chatroom_dialog_show (GTK_WINDOW (window));
}

static void
main_window_room_join_favorites_cb (GtkAction         *action,
				    EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GList *chatrooms, *l;

	chatrooms = empathy_chatroom_manager_get_chatrooms (priv->chatroom_manager, NULL);
	for (l = chatrooms; l; l = l->next) {
		main_window_favorite_chatroom_join (l->data);
	}
	g_list_free (chatrooms);
}

static void
main_window_room_manage_favorites_cb (GtkAction         *action,
				      EmpathyMainWindow *window)
{
	empathy_chatrooms_window_show (GTK_WINDOW (window));
}

static void
main_window_edit_cb (GtkAction         *action,
		     EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GtkWidget *submenu;

	/* FIXME: It should use the UIManager to merge the contact/group submenu */
	submenu = empathy_individual_view_get_individual_menu (
			priv->individual_view);
	if (submenu) {
		GtkMenuItem *item;
		GtkWidget   *label;

		item = GTK_MENU_ITEM (priv->edit_context);
		label = gtk_bin_get_child (GTK_BIN (item));
		gtk_label_set_text (GTK_LABEL (label), _("Contact"));

		gtk_widget_show (priv->edit_context);
		gtk_widget_show (priv->edit_context_separator);

		gtk_menu_item_set_submenu (item, submenu);

		return;
	}

	submenu = empathy_individual_view_get_group_menu (
			priv->individual_view);
	if (submenu) {
		GtkMenuItem *item;
		GtkWidget   *label;

		item = GTK_MENU_ITEM (priv->edit_context);
		label = gtk_bin_get_child (GTK_BIN (item));
		gtk_label_set_text (GTK_LABEL (label), _("Group"));

		gtk_widget_show (priv->edit_context);
		gtk_widget_show (priv->edit_context_separator);

		gtk_menu_item_set_submenu (item, submenu);

		return;
	}

	gtk_widget_hide (priv->edit_context);
	gtk_widget_hide (priv->edit_context_separator);

	return;
}

static void
main_window_edit_accounts_cb (GtkAction         *action,
			      EmpathyMainWindow *window)
{
	empathy_accounts_dialog_show_application (gdk_screen_get_default (),
			NULL, FALSE, FALSE);
}

static void
main_window_edit_personal_information_cb (GtkAction         *action,
					  EmpathyMainWindow *window)
{
	empathy_contact_personal_dialog_show (GTK_WINDOW (window));
}

static void
main_window_edit_blocked_contacts_cb (GtkAction         *action,
		                      EmpathyMainWindow *window)
{
	GtkWidget *dialog;

	dialog = empathy_contact_blocking_dialog_new (GTK_WINDOW (window));
	gtk_widget_show (dialog);
	g_signal_connect (dialog, "response",
			G_CALLBACK (gtk_widget_destroy), NULL);
}

void
empathy_main_window_show_preferences (EmpathyMainWindow *window,
    const gchar *tab)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);

	if (priv->preferences == NULL) {
		priv->preferences = empathy_preferences_new (GTK_WINDOW (window),
		                                             priv->shell_running);
		g_object_add_weak_pointer (G_OBJECT (priv->preferences),
					   (gpointer) &priv->preferences);

		gtk_widget_show (priv->preferences);
	} else {
		gtk_window_present (GTK_WINDOW (priv->preferences));
	}

	if (tab != NULL)
		empathy_preferences_show_tab (
			EMPATHY_PREFERENCES (priv->preferences), tab);
}

static void
main_window_edit_preferences_cb (GtkAction         *action,
				 EmpathyMainWindow *window)
{
	empathy_main_window_show_preferences (window, NULL);
}

static void
main_window_help_about_cb (GtkAction         *action,
			   EmpathyMainWindow *window)
{
	empathy_about_dialog_new (GTK_WINDOW (window));
}

static void
main_window_help_debug_cb (GtkAction         *action,
			   EmpathyMainWindow *window)
{
	empathy_launch_program (BIN_DIR, "empathy-debugger", NULL);
}

static void
main_window_help_contents_cb (GtkAction         *action,
			      EmpathyMainWindow *window)
{
	empathy_url_show (GTK_WIDGET (window), "ghelp:empathy");
}

static gboolean
main_window_throbber_button_press_event_cb (GtkWidget         *throbber,
					    GdkEventButton    *event,
					    EmpathyMainWindow *window)
{
	if (event->type != GDK_BUTTON_PRESS ||
	    event->button != 1) {
		return FALSE;
	}

	empathy_accounts_dialog_show_application (
			gtk_widget_get_screen (GTK_WIDGET (throbber)),
			NULL, FALSE, FALSE);

	return FALSE;
}

static void
main_window_account_removed_cb (TpAccountManager  *manager,
				TpAccount         *account,
				EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GList *a;

	a = tp_account_manager_get_valid_accounts (manager);

	gtk_action_set_sensitive (priv->view_history,
		g_list_length (a) > 0);

	g_list_free (a);

	/* remove errors if any */
	main_window_remove_error (window, account);

	/* remove the balance action if required */
	main_window_remove_balance_action (window, account);
}

static void
main_window_account_validity_changed_cb (TpAccountManager  *manager,
					 TpAccount         *account,
					 gboolean           valid,
					 EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);

	if (valid) {
		gulong handler_id;
		handler_id = GPOINTER_TO_UINT (g_hash_table_lookup (
			priv->status_changed_handlers, account));

		/* connect signal only if it was not connected yet */
		if (handler_id == 0) {
			handler_id = g_signal_connect (account,
				"status-changed",
				G_CALLBACK (main_window_connection_changed_cb),
				window);
			g_hash_table_insert (priv->status_changed_handlers,
				account, GUINT_TO_POINTER (handler_id));
		}
	}

	main_window_account_removed_cb (manager, account, window);
}

static void
main_window_notify_show_offline_cb (GSettings   *gsettings,
				    const gchar *key,
				    gpointer     toggle_action)
{
	gtk_toggle_action_set_active (toggle_action,
			g_settings_get_boolean (gsettings, key));
}

static void
main_window_connection_items_setup (EmpathyMainWindow *window,
				    GtkBuilder        *gui)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GList         *list;
	GObject       *action;
	guint          i;
	const gchar *actions_connected[] = {
		"room_join_new",
		"room_join_favorites",
		"chat_new_message",
		"chat_new_call",
		"chat_search_contacts",
		"chat_add_contact",
		"edit_personal_information",
		"edit_blocked_contacts",
		"edit_search_contacts"
	};

	for (i = 0, list = NULL; i < G_N_ELEMENTS (actions_connected); i++) {
		action = gtk_builder_get_object (gui, actions_connected[i]);
		list = g_list_prepend (list, action);
	}

	priv->actions_connected = list;
}

static void
account_manager_prepared_cb (GObject      *source_object,
			     GAsyncResult *result,
			     gpointer      user_data)
{
	GList *accounts, *j;
	TpAccountManager *manager = TP_ACCOUNT_MANAGER (source_object);
	EmpathyMainWindow *window = user_data;
	EmpathyMainWindowPriv *priv = GET_PRIV (window);
	GError *error = NULL;

	if (!tp_proxy_prepare_finish (manager, result, &error)) {
		DEBUG ("Failed to prepare account manager: %s", error->message);
		g_error_free (error);
		return;
	}

	accounts = tp_account_manager_get_valid_accounts (priv->account_manager);
	for (j = accounts; j != NULL; j = j->next) {
		TpAccount *account = TP_ACCOUNT (j->data);
		gulong handler_id;

		handler_id = g_signal_connect (account, "status-changed",
				  G_CALLBACK (main_window_connection_changed_cb),
				  window);
		g_hash_table_insert (priv->status_changed_handlers,
				     account, GUINT_TO_POINTER (handler_id));

		main_window_setup_balance (window, account);
	}

	g_signal_connect (manager, "account-validity-changed",
			  G_CALLBACK (main_window_account_validity_changed_cb),
			  window);

	main_window_update_status (window);

	/* Disable the "Previous Conversations" menu entry if there is no account */
	gtk_action_set_sensitive (priv->view_history,
		g_list_length (accounts) > 0);

	g_list_free (accounts);
}

void
empathy_main_window_set_shell_running (EmpathyMainWindow *window,
                                       gboolean          shell_running)
{
	EmpathyMainWindowPriv *priv = GET_PRIV (window);

	if (priv->shell_running == shell_running)
		return;

	priv->shell_running = shell_running;
	g_object_notify (G_OBJECT (window), "shell-running");
}

static GObject *
empathy_main_window_constructor (GType type,
                                 guint n_construct_params,
                                 GObjectConstructParam *construct_params)
{
	static GObject *window = NULL;

	if (window != NULL)
		return g_object_ref (window);

	window = G_OBJECT_CLASS (empathy_main_window_parent_class)->constructor (
		type, n_construct_params, construct_params);

	g_object_add_weak_pointer (window, (gpointer) &window);

	return window;
}

static void
empathy_main_window_set_property (GObject       *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec    *pspec)
{
	EmpathyMainWindow *self = EMPATHY_MAIN_WINDOW (object);
	EmpathyMainWindowPriv *priv = GET_PRIV (self);

	switch (property_id)
	{
		case PROP_SHELL_RUNNING:
			priv->shell_running = g_value_get_boolean (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
			break;
	}
}

static void
empathy_main_window_get_property (GObject    *object,
                                  guint      property_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	EmpathyMainWindow *self = EMPATHY_MAIN_WINDOW (object);
	EmpathyMainWindowPriv *priv = GET_PRIV (self);

	switch (property_id)
	{
		case PROP_SHELL_RUNNING:
			g_value_set_boolean (value, priv->shell_running);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
			break;
	}
}

static void
empathy_main_window_class_init (EmpathyMainWindowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;

	object_class->finalize = empathy_main_window_finalize;
	object_class->constructor = empathy_main_window_constructor;

	object_class->set_property = empathy_main_window_set_property;
	object_class->get_property = empathy_main_window_get_property;

	pspec = g_param_spec_boolean ("shell-running",
	                              "Shell running",
	                              "Whether the Shell is running or not",
	                              FALSE,
	                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_SHELL_RUNNING, pspec);

	g_type_class_add_private (object_class, sizeof (EmpathyMainWindowPriv));
}

static void
empathy_main_window_init (EmpathyMainWindow *window)
{
	EmpathyMainWindowPriv    *priv;
	EmpathyIndividualManager *individual_manager;
	GtkBuilder               *gui, *gui_mgr;
	GtkWidget                *sw;
	GtkToggleAction          *show_offline_widget;
	GtkAction                *show_map_widget;
	GtkToolItem              *item;
	gboolean                  show_offline;
	gchar                    *filename;
	GtkTreeModel             *model;
	GtkWidget                *search_vbox;
	GtkWidget                *menubar;

	priv = window->priv = G_TYPE_INSTANCE_GET_PRIVATE (window,
			EMPATHY_TYPE_MAIN_WINDOW, EmpathyMainWindowPriv);

	priv->gsettings_ui = g_settings_new (EMPATHY_PREFS_UI_SCHEMA);
	priv->gsettings_contacts = g_settings_new (EMPATHY_PREFS_CONTACTS_SCHEMA);

	priv->sound_mgr = empathy_sound_manager_dup_singleton ();

	gtk_window_set_title (GTK_WINDOW (window), _("Contact List"));
	gtk_window_set_role (GTK_WINDOW (window), "contact_list");
	gtk_window_set_default_size (GTK_WINDOW (window), 225, 325);

	/* don't finalize the widget on delete-event, just hide it */
	g_signal_connect (window, "delete-event",
		G_CALLBACK (gtk_widget_hide_on_delete), NULL);

	/* Set up interface */
	filename = empathy_file_lookup ("empathy-main-window.ui", "src");
	gui = empathy_builder_get_file (filename,
				       "main_vbox", &priv->main_vbox,
				       "balance_vbox", &priv->balance_vbox,
				       "errors_vbox", &priv->errors_vbox,
				       "auth_vbox", &priv->auth_vbox,
				       "search_vbox", &search_vbox,
				       "presence_toolbar", &priv->presence_toolbar,
				       "notebook", &priv->notebook,
				       "no_entry_label", &priv->no_entry_label,
				       "roster_scrolledwindow", &sw,
				       NULL);
	g_free (filename);

	/* Set UI manager */
	filename = empathy_file_lookup ("empathy-main-window-menubar.ui", "src");
	gui_mgr = empathy_builder_get_file (filename,
				       "ui_manager", &priv->ui_manager,
				       "view_show_offline", &show_offline_widget,
				       "view_show_protocols", &priv->show_protocols,
				       "view_sort_by_name", &priv->sort_by_name,
				       "view_sort_by_status", &priv->sort_by_status,
				       "view_normal_size_with_avatars", &priv->normal_with_avatars,
				       "view_normal_size", &priv->normal_size,
				       "view_compact_size", &priv->compact_size,
				       "view_history", &priv->view_history,
				       "view_show_map", &show_map_widget,
				       "room_join_favorites", &priv->room_join_favorites,
				       "view_balance_show_in_roster", &priv->view_balance_show_in_roster,
				       "menubar", &menubar,
				       NULL);
	g_free (filename);

	/* The UI manager is living in its own .ui file as Glade doesn't support
	 * those. The GtkMenubar has to be in this file as well to we manually add
	 * it to the first position of the vbox. */
	gtk_box_pack_start (GTK_BOX (priv->main_vbox), menubar, FALSE, FALSE, 0);
	gtk_box_reorder_child (GTK_BOX (priv->main_vbox), menubar, 0);

	gtk_container_add (GTK_CONTAINER (window), priv->main_vbox);
	gtk_widget_show (priv->main_vbox);

	g_signal_connect (window, "key-press-event",
			  G_CALLBACK (main_window_key_press_event_cb), NULL);

	empathy_builder_connect (gui_mgr, window,
			      "chat_quit", "activate", main_window_chat_quit_cb,
			      "chat_new_message", "activate", main_window_chat_new_message_cb,
			      "chat_new_call", "activate", main_window_chat_new_call_cb,
			      "view_history", "activate", main_window_view_history_cb,
			      "room_join_new", "activate", main_window_room_join_new_cb,
			      "room_join_favorites", "activate", main_window_room_join_favorites_cb,
			      "room_manage_favorites", "activate", main_window_room_manage_favorites_cb,
			      "chat_add_contact", "activate", main_window_chat_add_contact_cb,
			      "chat_search_contacts", "activate", main_window_chat_search_contacts_cb,
			      "view_show_ft_manager", "activate", main_window_view_show_ft_manager,
			      "view_show_offline", "toggled", main_window_view_show_offline_cb,
			      "view_show_protocols", "toggled", main_window_view_show_protocols_cb,
			      "view_sort_by_name", "changed", main_window_view_sort_contacts_cb,
			      "view_normal_size_with_avatars", "changed", main_window_view_contacts_list_size_cb,
			      "view_show_map", "activate", main_window_view_show_map_cb,
			      "edit", "activate", main_window_edit_cb,
			      "edit_accounts", "activate", main_window_edit_accounts_cb,
			      "edit_personal_information", "activate", main_window_edit_personal_information_cb,
			      "edit_blocked_contacts", "activate", main_window_edit_blocked_contacts_cb,
			      "edit_preferences", "activate", main_window_edit_preferences_cb,
			      "edit_search_contacts", "activate", main_window_edit_search_contacts_cb,
			      "help_about", "activate", main_window_help_about_cb,
			      "help_debug", "activate", main_window_help_debug_cb,
			      "help_contents", "activate", main_window_help_contents_cb,
			      NULL);

	/* Set up connection related widgets. */
	main_window_connection_items_setup (window, gui_mgr);

	g_object_ref (priv->ui_manager);
	g_object_unref (gui);
	g_object_unref (gui_mgr);

#ifndef HAVE_LIBCHAMPLAIN
	gtk_action_set_visible (show_map_widget, FALSE);
#endif

	priv->account_manager = tp_account_manager_dup ();

	tp_proxy_prepare_async (priv->account_manager, NULL,
					  account_manager_prepared_cb, window);

	priv->errors = g_hash_table_new_full (g_direct_hash,
					      g_direct_equal,
					      g_object_unref,
					      NULL);

	priv->auths = g_hash_table_new (NULL, NULL);

	priv->status_changed_handlers = g_hash_table_new_full (g_direct_hash,
							       g_direct_equal,
							       NULL,
							       NULL);

	/* Set up menu */
	main_window_favorite_chatroom_menu_setup (window);

	priv->edit_context = gtk_ui_manager_get_widget (priv->ui_manager,
		"/menubar/edit/edit_context");
	priv->edit_context_separator = gtk_ui_manager_get_widget (
		priv->ui_manager,
		"/menubar/edit/edit_context_separator");
	gtk_widget_hide (priv->edit_context);
	gtk_widget_hide (priv->edit_context_separator);

	/* Set up contact list. */
	empathy_status_presets_get_all ();

	/* Set up presence chooser */
	priv->presence_chooser = empathy_presence_chooser_new ();
	gtk_widget_show (priv->presence_chooser);
	item = gtk_tool_item_new ();
	gtk_widget_show (GTK_WIDGET (item));
	gtk_widget_set_size_request (priv->presence_chooser, 10, -1);
	gtk_container_add (GTK_CONTAINER (item), priv->presence_chooser);
	gtk_tool_item_set_is_important (item, TRUE);
	gtk_tool_item_set_expand (item, TRUE);
	gtk_toolbar_insert (GTK_TOOLBAR (priv->presence_toolbar), item, -1);

	/* Set up the throbber */
	priv->throbber = gtk_spinner_new ();
	gtk_widget_set_size_request (priv->throbber, 16, -1);
	gtk_widget_set_events (priv->throbber, GDK_BUTTON_PRESS_MASK);
	g_signal_connect (priv->throbber, "button-press-event",
		G_CALLBACK (main_window_throbber_button_press_event_cb),
		window);
	gtk_widget_show (priv->throbber);

	item = gtk_tool_item_new ();
	gtk_container_set_border_width (GTK_CONTAINER (item), 6);
	gtk_toolbar_insert (GTK_TOOLBAR (priv->presence_toolbar), item, -1);
	gtk_container_add (GTK_CONTAINER (item), priv->throbber);
	priv->throbber_tool_item = GTK_WIDGET (item);

	/* XXX: this class is designed to live for the duration of the program,
	 * so it's got a race condition between its signal handlers and its
	 * finalization. The class is planned to be removed, so we won't fix
	 * this before then. */
	priv->contact_manager = EMPATHY_CONTACT_LIST (
			empathy_contact_manager_dup_singleton ());
	individual_manager = empathy_individual_manager_dup_singleton ();
	priv->individual_store = EMPATHY_INDIVIDUAL_STORE (
			empathy_individual_store_manager_new (individual_manager));
	g_object_unref (individual_manager);

	/* For the moment, we disallow Persona drops onto the main contact list (e.g. from things such as
	 * the EmpathyPersonaView in the linking dialogue). No code is hooked up to do anything on a Persona
	 * drop, so allowing them would achieve nothing except confusion. */
	priv->individual_view = empathy_individual_view_new (
			priv->individual_store,
			EMPATHY_INDIVIDUAL_VIEW_FEATURE_ALL ^ EMPATHY_INDIVIDUAL_VIEW_FEATURE_PERSONA_DROP,
			EMPATHY_INDIVIDUAL_FEATURE_ALL ^ EMPATHY_INDIVIDUAL_FEATURE_ADD_CONTACT);

	gtk_widget_show (GTK_WIDGET (priv->individual_view));
	gtk_container_add (GTK_CONTAINER (sw),
			   GTK_WIDGET (priv->individual_view));
	g_signal_connect (priv->individual_view, "row-activated",
			  G_CALLBACK (main_window_row_activated_cb),
			  window);

	/* Set up search bar */
	priv->search_bar = empathy_live_search_new (
		GTK_WIDGET (priv->individual_view));
	empathy_individual_view_set_live_search (priv->individual_view,
		EMPATHY_LIVE_SEARCH (priv->search_bar));
	gtk_box_pack_start (GTK_BOX (search_vbox), priv->search_bar,
		FALSE, TRUE, 0);

	g_signal_connect_swapped (window, "map",
		G_CALLBACK (gtk_widget_grab_focus), priv->individual_view);

	/* Connect to proper signals to check if contact list is empty or not */
	model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->individual_view));
	priv->empty = TRUE;
	g_signal_connect (model, "row-inserted",
			  G_CALLBACK (main_window_row_inserted_cb),
			  window);
	g_signal_connect (model, "row-deleted",
			  G_CALLBACK (main_window_row_deleted_cb),
			  window);

	/* Load user-defined accelerators. */
	main_window_accels_load ();

	/* Set window size. */
	empathy_geometry_bind (GTK_WINDOW (window), GEOMETRY_NAME);

	/* bind view_balance_show_in_roster */
	g_settings_bind (priv->gsettings_ui, "show-balance-in-roster",
		priv->view_balance_show_in_roster, "active",
		G_SETTINGS_BIND_DEFAULT);
	g_object_bind_property (priv->view_balance_show_in_roster, "active",
		priv->balance_vbox, "visible",
		G_BINDING_SYNC_CREATE);

	/* Enable event handling */
	priv->call_observer = empathy_call_observer_dup_singleton ();
	priv->event_manager = empathy_event_manager_dup_singleton ();

	g_signal_connect (priv->event_manager, "event-added",
			  G_CALLBACK (main_window_event_added_cb), window);
	g_signal_connect (priv->event_manager, "event-removed",
			  G_CALLBACK (main_window_event_removed_cb), window);
	g_signal_connect (priv->account_manager, "account-validity-changed",
			  G_CALLBACK (main_window_account_validity_changed_cb),
			  window);
	g_signal_connect (priv->account_manager, "account-removed",
			  G_CALLBACK (main_window_account_removed_cb),
			  window);
	g_signal_connect (priv->account_manager, "account-disabled",
			  G_CALLBACK (main_window_account_disabled_cb),
			  window);

	/* Show offline ? */
	show_offline = g_settings_get_boolean (priv->gsettings_ui,
					       EMPATHY_PREFS_UI_SHOW_OFFLINE);
	g_signal_connect (priv->gsettings_ui,
			  "changed::" EMPATHY_PREFS_UI_SHOW_OFFLINE,
			  G_CALLBACK (main_window_notify_show_offline_cb),
			  show_offline_widget);

	gtk_toggle_action_set_active (show_offline_widget, show_offline);

	/* Show protocol ? */
	g_signal_connect (priv->gsettings_ui,
			  "changed::" EMPATHY_PREFS_UI_SHOW_PROTOCOLS,
			  G_CALLBACK (main_window_notify_show_protocols_cb),
			  window);

	main_window_notify_show_protocols_cb (priv->gsettings_ui,
					      EMPATHY_PREFS_UI_SHOW_PROTOCOLS,
					      window);

	/* Sort by name / by status ? */
	g_signal_connect (priv->gsettings_contacts,
			  "changed::" EMPATHY_PREFS_CONTACTS_SORT_CRITERIUM,
			  G_CALLBACK (main_window_notify_sort_contact_cb),
			  window);

	main_window_notify_sort_contact_cb (priv->gsettings_contacts,
					    EMPATHY_PREFS_CONTACTS_SORT_CRITERIUM,
					    window);

	/* Contacts list size */
	g_signal_connect (priv->gsettings_ui,
			  "changed::" EMPATHY_PREFS_UI_COMPACT_CONTACT_LIST,
			  G_CALLBACK (main_window_notify_contact_list_size_cb),
			  window);
	g_signal_connect (priv->gsettings_ui,
			  "changed::" EMPATHY_PREFS_UI_SHOW_AVATARS,
			  G_CALLBACK (main_window_notify_contact_list_size_cb),
			  window);

	main_window_notify_contact_list_size_cb (priv->gsettings_ui,
						 EMPATHY_PREFS_UI_SHOW_AVATARS,
						 window);
}

GtkWidget *
empathy_main_window_dup (void)
{
	return g_object_new (EMPATHY_TYPE_MAIN_WINDOW, NULL);
}
