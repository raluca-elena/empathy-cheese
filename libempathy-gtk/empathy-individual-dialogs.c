/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007-2010 Collabora Ltd.
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
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <telepathy-glib/util.h>
#include <folks/folks.h>

#include <libempathy/empathy-individual-manager.h>
#include <libempathy/empathy-utils.h>

#include "empathy-individual-dialogs.h"
#include "empathy-contact-widget.h"
#include "empathy-ui-utils.h"

static GtkWidget *new_individual_dialog = NULL;

/*
 *  New contact dialog
 */

static void
can_add_contact_to_account (TpAccount *account,
    EmpathyAccountChooserFilterResultCallback callback,
    gpointer callback_data,
    gpointer user_data)
{
  EmpathyIndividualManager *individual_manager;
  TpConnection *connection;
  gboolean result;

  connection = tp_account_get_connection (account);
  if (connection == NULL)
    {
      callback (FALSE, callback_data);
      return;
    }

  individual_manager = empathy_individual_manager_dup_singleton ();
  result = empathy_connection_can_add_personas (connection);
  g_object_unref (individual_manager);

  callback (result, callback_data);
}

static void
new_individual_response_cb (GtkDialog *dialog,
    gint response,
    GtkWidget *contact_widget)
{
  EmpathyIndividualManager *individual_manager;
  EmpathyContact *contact;

  individual_manager = empathy_individual_manager_dup_singleton ();
  contact = empathy_contact_widget_get_contact (contact_widget);

  if (contact && response == GTK_RESPONSE_OK)
    empathy_individual_manager_add_from_contact (individual_manager, contact);

  new_individual_dialog = NULL;
  gtk_widget_destroy (GTK_WIDGET (dialog));
  g_object_unref (individual_manager);
}

void
empathy_new_individual_dialog_show (GtkWindow *parent)
{
  empathy_new_individual_dialog_show_with_individual (parent, NULL);
}

void
empathy_new_individual_dialog_show_with_individual (GtkWindow *parent,
    FolksIndividual *individual)
{
  GtkWidget *dialog;
  GtkWidget *button;
  EmpathyContact *contact = NULL;
  GtkWidget *contact_widget;

  g_return_if_fail (individual == NULL || FOLKS_IS_INDIVIDUAL (individual));

  if (new_individual_dialog)
    {
      gtk_window_present (GTK_WINDOW (new_individual_dialog));
      return;
    }

  /* Create dialog */
  dialog = gtk_dialog_new ();
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_window_set_title (GTK_WINDOW (dialog), _("New Contact"));

  /* Cancel button */
  button = gtk_button_new_with_label (GTK_STOCK_CANCEL);
  gtk_button_set_use_stock (GTK_BUTTON (button), TRUE);
  gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button,
      GTK_RESPONSE_CANCEL);
  gtk_widget_show (button);

  /* Add button */
  button = gtk_button_new_with_label (GTK_STOCK_ADD);
  gtk_button_set_use_stock (GTK_BUTTON (button), TRUE);
  gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button, GTK_RESPONSE_OK);
  gtk_widget_show (button);

  /* Contact info widget */
  if (individual != NULL)
    contact = empathy_contact_dup_from_folks_individual (individual);

  contact_widget = empathy_contact_widget_new (contact,
      EMPATHY_CONTACT_WIDGET_EDIT_ALIAS |
      EMPATHY_CONTACT_WIDGET_EDIT_ACCOUNT |
      EMPATHY_CONTACT_WIDGET_EDIT_ID |
      EMPATHY_CONTACT_WIDGET_EDIT_GROUPS);
  gtk_container_set_border_width (GTK_CONTAINER (contact_widget), 8);
  gtk_box_pack_start (
      GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
      contact_widget, TRUE, TRUE, 0);
  empathy_contact_widget_set_account_filter (contact_widget,
      can_add_contact_to_account, NULL);
  gtk_widget_show (contact_widget);

  new_individual_dialog = dialog;

  g_signal_connect (dialog, "response", G_CALLBACK (new_individual_response_cb),
      contact_widget);

  if (parent != NULL)
    gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);

  gtk_widget_show (dialog);

  tp_clear_object (&contact);
}
