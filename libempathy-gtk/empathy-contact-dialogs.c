/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007-2008 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <telepathy-glib/account-manager.h>

#include <libempathy/empathy-contact-manager.h>
#include <libempathy/empathy-contact-list.h>
#include <libempathy/empathy-tp-contact-factory.h>
#include <libempathy/empathy-utils.h>

#include "empathy-contact-dialogs.h"
#include "empathy-contact-widget.h"
#include "empathy-ui-utils.h"

static GList *subscription_dialogs = NULL;
static GList *information_dialogs = NULL;
static GList *edit_dialogs = NULL;
static GtkWidget *personal_dialog = NULL;
static GtkWidget *new_contact_dialog = NULL;

static gint
contact_dialogs_find (GtkDialog      *dialog,
		      EmpathyContact *contact)
{
	GtkWidget     *contact_widget;
	EmpathyContact *this_contact;

	contact_widget = g_object_get_data (G_OBJECT (dialog), "contact_widget");
	this_contact = empathy_contact_widget_get_contact (contact_widget);

	return contact != this_contact;
}

/*
 *  Subscription dialog
 */

static void
subscription_dialog_response_cb (GtkDialog *dialog,
				 gint       response,
				 GtkWidget *contact_widget)
{
	EmpathyContactManager *manager;
	EmpathyContact        *contact;

	manager = empathy_contact_manager_dup_singleton ();
	contact = empathy_contact_widget_get_contact (contact_widget);

	if (response == GTK_RESPONSE_YES) {
		empathy_contact_list_add (EMPATHY_CONTACT_LIST (manager),
					  contact, "");

		empathy_contact_set_alias (contact,
			empathy_contact_widget_get_alias (contact_widget));
	}
	else if (response == GTK_RESPONSE_NO) {
		empathy_contact_list_remove (EMPATHY_CONTACT_LIST (manager),
					     contact, "");
	}
	else if (response == GTK_RESPONSE_REJECT) {
		gboolean abusive;

		/* confirm the blocking */
		if (empathy_block_contact_dialog_show (GTK_WINDOW (dialog), contact,
						       NULL, &abusive)) {
			TpContact *tp_contact;

			empathy_contact_list_remove (
					EMPATHY_CONTACT_LIST (manager),
					contact, "");

			tp_contact = empathy_contact_get_tp_contact (contact);

			tp_contact_block_async (tp_contact, abusive, NULL, NULL);
		} else {
			/* if they don't confirm, return back to the
			 * first dialog */
			goto finally;
		}
	}

	subscription_dialogs = g_list_remove (subscription_dialogs, dialog);
	gtk_widget_destroy (GTK_WIDGET (dialog));

finally:
	g_object_unref (manager);
}

void
empathy_subscription_dialog_show (EmpathyContact *contact,
				  const gchar *message,
				  GtkWindow     *parent)
{
	GtkBuilder *gui;
	GtkWidget *dialog;
	GtkWidget *hbox_subscription;
	GtkWidget *vbox;
	GtkWidget *contact_widget;
	GtkWidget *block_user_button;
	GList     *l;
	gchar     *filename;
	TpConnection *conn;

	g_return_if_fail (EMPATHY_IS_CONTACT (contact));

	l = g_list_find_custom (subscription_dialogs,
				contact,
				(GCompareFunc) contact_dialogs_find);
	if (l) {
		gtk_window_present (GTK_WINDOW (l->data));
		return;
	}

	filename = empathy_file_lookup ("empathy-contact-dialogs.ui",
					"libempathy-gtk");
	gui = empathy_builder_get_file (filename,
				      "subscription_request_dialog", &dialog,
				      "hbox_subscription", &hbox_subscription,
				      "block-user-button", &block_user_button,
				      NULL);
	g_free (filename);
	g_object_unref (gui);

	vbox = gtk_vbox_new (FALSE, 6);

	gtk_box_pack_end (GTK_BOX (hbox_subscription), vbox,
			  TRUE, TRUE, 0);

	/* Contact info widget */
	contact_widget = empathy_contact_widget_new (contact,
						     EMPATHY_CONTACT_WIDGET_NO_SET_ALIAS |
						     EMPATHY_CONTACT_WIDGET_EDIT_ALIAS |
						     EMPATHY_CONTACT_WIDGET_EDIT_GROUPS |
						     EMPATHY_CONTACT_WIDGET_SHOW_DETAILS);
	gtk_box_pack_start (GTK_BOX (vbox),
			  contact_widget,
			  TRUE, TRUE,
			  0);

	if (!tp_str_empty (message)) {
		GtkWidget *label;
		gchar *tmp;

		label = gtk_label_new ("");
		tmp = g_strdup_printf ("<i>%s</i>", message);

		gtk_label_set_markup (GTK_LABEL (label), tmp);
		g_free (tmp);
		gtk_widget_show (label);

		gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
	}

	gtk_widget_show (contact_widget);
	gtk_widget_show (vbox);

	g_object_set_data (G_OBJECT (dialog), "contact_widget", contact_widget);
	subscription_dialogs = g_list_prepend (subscription_dialogs, dialog);

	g_signal_connect (dialog, "response",
			  G_CALLBACK (subscription_dialog_response_cb),
			  contact_widget);

	conn = empathy_contact_get_connection (contact);

	if (tp_proxy_has_interface_by_id (conn,
		TP_IFACE_QUARK_CONNECTION_INTERFACE_CONTACT_BLOCKING))
		gtk_widget_show (block_user_button);

	if (parent) {
		gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
	}

	gtk_widget_show (dialog);
}

/*
 *  Information dialog
 */

static void
contact_dialogs_response_cb (GtkDialog *dialog,
			     gint       response,
			     GList    **dialogs)
{
	*dialogs = g_list_remove (*dialogs, dialog);
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

void
empathy_contact_information_dialog_show (EmpathyContact *contact,
					 GtkWindow      *parent)
{
	GtkWidget *dialog;
	GtkWidget *button;
	GtkWidget *contact_widget;
	GList     *l;

	g_return_if_fail (EMPATHY_IS_CONTACT (contact));

	l = g_list_find_custom (information_dialogs,
				contact,
				(GCompareFunc) contact_dialogs_find);
	if (l) {
		gtk_window_present (GTK_WINDOW (l->data));
		return;
	}

	/* Create dialog */
	dialog = gtk_dialog_new ();
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
	gtk_window_set_title (GTK_WINDOW (dialog),
		empathy_contact_get_alias (contact));

	/* Close button */
	button = gtk_button_new_with_label (GTK_STOCK_CLOSE);
	gtk_button_set_use_stock (GTK_BUTTON (button), TRUE);
	gtk_dialog_add_action_widget (GTK_DIALOG (dialog),
				      button,
				      GTK_RESPONSE_CLOSE);
	gtk_widget_set_can_default (button, TRUE);
	gtk_window_set_default (GTK_WINDOW (dialog), button);
	gtk_widget_show (button);

	/* Contact info widget */
	contact_widget = empathy_contact_widget_new (contact,
		EMPATHY_CONTACT_WIDGET_SHOW_LOCATION |
		EMPATHY_CONTACT_WIDGET_SHOW_DETAILS);
	gtk_container_set_border_width (GTK_CONTAINER (contact_widget), 8);
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
			    contact_widget,
			    TRUE, TRUE, 0);
	gtk_widget_show (contact_widget);

	g_object_set_data (G_OBJECT (dialog), "contact_widget", contact_widget);
	information_dialogs = g_list_prepend (information_dialogs, dialog);

	g_signal_connect (dialog, "response",
			  G_CALLBACK (contact_dialogs_response_cb),
			  &information_dialogs);

	if (parent) {
		gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
	}

	gtk_widget_show (dialog);
}

void
empathy_contact_edit_dialog_show (EmpathyContact *contact,
				  GtkWindow      *parent)
{
	GtkWidget *dialog;
	GtkWidget *button;
	GtkWidget *contact_widget;
	GList     *l;

	g_return_if_fail (EMPATHY_IS_CONTACT (contact));

	l = g_list_find_custom (edit_dialogs,
				contact,
				(GCompareFunc) contact_dialogs_find);
	if (l) {
		gtk_window_present (GTK_WINDOW (l->data));
		return;
	}

	/* Create dialog */
	dialog = gtk_dialog_new ();
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
	gtk_window_set_title (GTK_WINDOW (dialog), _("Edit Contact Information"));

	/* Close button */
	button = gtk_button_new_with_label (GTK_STOCK_CLOSE);
	gtk_button_set_use_stock (GTK_BUTTON (button), TRUE);
	gtk_dialog_add_action_widget (GTK_DIALOG (dialog),
				      button,
				      GTK_RESPONSE_CLOSE);
	gtk_widget_set_can_default (button, TRUE);
	gtk_window_set_default (GTK_WINDOW (dialog), button);
	gtk_widget_show (button);

	/* Contact info widget */
	contact_widget = empathy_contact_widget_new (contact,
		EMPATHY_CONTACT_WIDGET_EDIT_ALIAS |
		EMPATHY_CONTACT_WIDGET_EDIT_GROUPS |
		EMPATHY_CONTACT_WIDGET_EDIT_FAVOURITE);
	gtk_container_set_border_width (GTK_CONTAINER (contact_widget), 8);
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
			    contact_widget,
			    TRUE, TRUE, 0);
	gtk_widget_show (contact_widget);

	g_object_set_data (G_OBJECT (dialog), "contact_widget", contact_widget);
	edit_dialogs = g_list_prepend (edit_dialogs, dialog);

	g_signal_connect (dialog, "response",
			  G_CALLBACK (contact_dialogs_response_cb),
			  &edit_dialogs);

	if (parent) {
		gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
	}

	gtk_widget_show (dialog);
}

void
empathy_contact_personal_dialog_show (GtkWindow *parent)
{
	GtkWidget *button;
	GtkWidget *contact_widget;

	if (personal_dialog) {
		gtk_window_present (GTK_WINDOW (personal_dialog));
		return;
	}

	/* Create dialog */
	personal_dialog = gtk_dialog_new ();
	gtk_window_set_resizable (GTK_WINDOW (personal_dialog), FALSE);
	gtk_window_set_title (GTK_WINDOW (personal_dialog), _("Personal Information"));

	/* Close button */
	button = gtk_button_new_with_label (GTK_STOCK_CLOSE);
	gtk_button_set_use_stock (GTK_BUTTON (button), TRUE);
	gtk_dialog_add_action_widget (GTK_DIALOG (personal_dialog),
				      button,
				      GTK_RESPONSE_CLOSE);
	gtk_widget_set_can_default (button, TRUE);
	gtk_window_set_default (GTK_WINDOW (personal_dialog), button);
	gtk_widget_show (button);

	/* Contact info widget */
	contact_widget = empathy_contact_widget_new (NULL,
		EMPATHY_CONTACT_WIDGET_EDIT_ACCOUNT |
		EMPATHY_CONTACT_WIDGET_EDIT_ALIAS |
		EMPATHY_CONTACT_WIDGET_EDIT_AVATAR |
		EMPATHY_CONTACT_WIDGET_EDIT_DETAILS);
	gtk_container_set_border_width (GTK_CONTAINER (contact_widget), 8);
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (personal_dialog))),
			    contact_widget,
			    TRUE, TRUE, 0);
	empathy_contact_widget_set_account_filter (contact_widget,
		empathy_account_chooser_filter_is_connected, NULL);
	gtk_widget_show (contact_widget);

	g_signal_connect (personal_dialog, "response",
			  G_CALLBACK (gtk_widget_destroy), NULL);
	g_object_add_weak_pointer (G_OBJECT (personal_dialog),
				   (gpointer) &personal_dialog);

	if (parent) {
		gtk_window_set_transient_for (GTK_WINDOW (personal_dialog), parent);
	}

	gtk_widget_show (personal_dialog);
}

/*
 *  New contact dialog
 */

static void
can_add_contact_to_account (TpAccount                                 *account,
			    EmpathyAccountChooserFilterResultCallback  callback,
			    gpointer                                   callback_data,
			    gpointer                                   user_data)
{
	EmpathyContactManager *contact_manager;
	TpConnection          *connection;
	gboolean               result;

	connection = tp_account_get_connection (account);
	if (connection == NULL) {
		callback (FALSE, callback_data);
		return;
	}

	contact_manager = empathy_contact_manager_dup_singleton ();
	result = empathy_contact_manager_get_flags_for_connection (
		contact_manager, connection) & EMPATHY_CONTACT_LIST_CAN_ADD;
	g_object_unref (contact_manager);

	callback (result, callback_data);
}

static void
new_contact_response_cb (GtkDialog *dialog,
			 gint       response,
			 GtkWidget *contact_widget)
{
	EmpathyContactManager *manager;
	EmpathyContact         *contact;

	manager = empathy_contact_manager_dup_singleton ();
	contact = empathy_contact_widget_get_contact (contact_widget);

	if (contact && response == GTK_RESPONSE_OK) {
		empathy_contact_list_add (EMPATHY_CONTACT_LIST (manager),
					  contact, "");
	}

	new_contact_dialog = NULL;
	gtk_widget_destroy (GTK_WIDGET (dialog));
	g_object_unref (manager);
}

void
empathy_new_contact_dialog_show (GtkWindow *parent)
{
	empathy_new_contact_dialog_show_with_contact (parent, NULL);
}

void
empathy_new_contact_dialog_show_with_contact (GtkWindow *parent,
                                              EmpathyContact *contact)
{
	GtkWidget *dialog;
	GtkWidget *button;
	GtkWidget *contact_widget;

	if (new_contact_dialog) {
		gtk_window_present (GTK_WINDOW (new_contact_dialog));
		return;
	}

	/* Create dialog */
	dialog = gtk_dialog_new ();
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
	gtk_window_set_title (GTK_WINDOW (dialog), _("New Contact"));

	/* Cancel button */
	button = gtk_button_new_with_label (GTK_STOCK_CANCEL);
	gtk_button_set_use_stock (GTK_BUTTON (button), TRUE);
	gtk_dialog_add_action_widget (GTK_DIALOG (dialog),
				      button,
				      GTK_RESPONSE_CANCEL);
	gtk_widget_show (button);

	/* Add button */
	button = gtk_button_new_with_label (GTK_STOCK_ADD);
	gtk_button_set_use_stock (GTK_BUTTON (button), TRUE);
	gtk_dialog_add_action_widget (GTK_DIALOG (dialog),
				      button,
				      GTK_RESPONSE_OK);
	gtk_widget_show (button);

	/* Contact info widget */
	contact_widget = empathy_contact_widget_new (contact,
						     EMPATHY_CONTACT_WIDGET_EDIT_ALIAS |
						     EMPATHY_CONTACT_WIDGET_EDIT_ACCOUNT |
						     EMPATHY_CONTACT_WIDGET_EDIT_ID |
						     EMPATHY_CONTACT_WIDGET_EDIT_GROUPS);
	gtk_container_set_border_width (GTK_CONTAINER (contact_widget), 8);
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
			    contact_widget,
			    TRUE, TRUE, 0);
	empathy_contact_widget_set_account_filter (contact_widget,
						   can_add_contact_to_account,
						   NULL);
	gtk_widget_show (contact_widget);

	new_contact_dialog = dialog;

	g_signal_connect (dialog, "response",
			  G_CALLBACK (new_contact_response_cb),
			  contact_widget);

	if (parent) {
		gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
	}

	gtk_widget_show (dialog);
}

/**
 * empathy_block_contact_dialog_show:
 * @parent: the parent of this dialog (or %NULL)
 * @contact: the contact for this dialog
 * @abusive: a pointer to store the value of the abusive contact check box
 *  (or %NULL)
 *
 * Returns: %TRUE if the user wishes to block the contact
 */
gboolean
empathy_block_contact_dialog_show (GtkWindow      *parent,
				   EmpathyContact *contact,
				   GdkPixbuf      *avatar,
				   gboolean       *abusive)
{
	GtkWidget *dialog;
	GtkWidget *abusive_check = NULL;
	int res;
	TpConnection *conn;

	dialog = gtk_message_dialog_new (parent,
			GTK_DIALOG_MODAL,
			GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
			_("Block %s?"),
			empathy_contact_get_alias (contact));

	gtk_message_dialog_format_secondary_text (
			GTK_MESSAGE_DIALOG (dialog),
			_("Are you sure you want to block '%s' from "
			  "contacting you again?"),
			empathy_contact_get_alias (contact));
	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			_("_Block"), GTK_RESPONSE_REJECT,
			NULL);

	if (avatar != NULL) {
		GtkWidget *image = gtk_image_new_from_pixbuf (avatar);
		gtk_message_dialog_set_image (GTK_MESSAGE_DIALOG (dialog), image);
		gtk_widget_show (image);
	}

	conn = empathy_contact_get_connection (contact);

	/* ask the user if they want to also report the contact as abusive */
	if (tp_connection_can_report_abusive (conn)) {
		GtkWidget *vbox;

		vbox = gtk_message_dialog_get_message_area (
				GTK_MESSAGE_DIALOG (dialog));
		abusive_check = gtk_check_button_new_with_mnemonic (
				_("_Report this contact as abusive"));

		gtk_box_pack_start (GTK_BOX (vbox), abusive_check,
				    FALSE, TRUE, 0);
		gtk_widget_show (abusive_check);
	}

	res = gtk_dialog_run (GTK_DIALOG (dialog));
	if (abusive != NULL) {
		if (abusive_check != NULL) {
			*abusive = gtk_toggle_button_get_active (
					GTK_TOGGLE_BUTTON (abusive_check));
		} else {
			*abusive = FALSE;
		}
	}

	gtk_widget_destroy (dialog);

	return res == GTK_RESPONSE_REJECT;
}
