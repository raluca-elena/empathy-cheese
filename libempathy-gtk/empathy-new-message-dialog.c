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
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <telepathy-glib/interfaces.h>

#include <libempathy/empathy-tp-contact-factory.h>
#include <libempathy/empathy-contact-manager.h>
#include <libempathy/empathy-request-util.h>
#include <libempathy/empathy-utils.h>

#define DEBUG_FLAG EMPATHY_DEBUG_CONTACT
#include <libempathy/empathy-debug.h>

#include <libempathy-gtk/empathy-ui-utils.h>
#include <libempathy-gtk/empathy-images.h>

#include "empathy-new-message-dialog.h"
#include "empathy-account-chooser.h"

typedef struct {
  EmpathyAccountChooserFilterResultCallback callback;
  gpointer                                  user_data;
} FilterCallbackData;

static EmpathyNewMessageDialog *dialog_singleton = NULL;

G_DEFINE_TYPE(EmpathyNewMessageDialog, empathy_new_message_dialog,
               EMPATHY_TYPE_CONTACT_SELECTOR_DIALOG)

/**
 * SECTION:empathy-new-message-dialog
 * @title: EmpathyNewMessageDialog
 * @short_description: A dialog to show a new message
 * @include: libempathy-gtk/empathy-new-message-dialog.h
 *
 * #EmpathyNewMessageDialog is a dialog which allows a text chat
 * to be started with any contact on any enabled account.
 */

enum
{
  EMP_NEW_MESSAGE_TEXT,
  EMP_NEW_MESSAGE_SMS,
};

static void
empathy_new_message_dialog_response (GtkDialog *dialog, int response_id)
{
  TpAccount *account;
  const gchar *contact_id;

  if (response_id < EMP_NEW_MESSAGE_TEXT) goto out;

  contact_id = empathy_contact_selector_dialog_get_selected (
      EMPATHY_CONTACT_SELECTOR_DIALOG (dialog), NULL, &account);

  if (EMP_STR_EMPTY (contact_id) || account == NULL) goto out;

  switch (response_id)
    {
      case EMP_NEW_MESSAGE_TEXT:
        empathy_chat_with_contact_id (account, contact_id,
            gtk_get_current_event_time ());
        break;

      case EMP_NEW_MESSAGE_SMS:
        empathy_sms_contact_id (account, contact_id,
            gtk_get_current_event_time ());
        break;

      default:
        g_warn_if_reached ();
    }

out:
  gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
conn_prepared_cb (GObject *conn,
    GAsyncResult *result,
    gpointer user_data)
{
  FilterCallbackData *data = user_data;
  GError *myerr = NULL;
  TpCapabilities *caps;

  if (!tp_proxy_prepare_finish (conn, result, &myerr))
    {
      data->callback (FALSE, data->user_data);
      g_slice_free (FilterCallbackData, data);
    }

  caps = tp_connection_get_capabilities (TP_CONNECTION (conn));
  data->callback (tp_capabilities_supports_text_chats (caps),
      data->user_data);

  g_slice_free (FilterCallbackData, data);
}

static void
empathy_new_message_account_filter (EmpathyContactSelectorDialog *dialog,
    EmpathyAccountChooserFilterResultCallback callback,
    gpointer callback_data,
    TpAccount *account)
{
  TpConnection *connection;
  FilterCallbackData *cb_data;
  GQuark features[] = { TP_CONNECTION_FEATURE_CAPABILITIES, 0 };

  if (tp_account_get_connection_status (account, NULL) !=
      TP_CONNECTION_STATUS_CONNECTED)
    {
      callback (FALSE, callback_data);
      return;
    }

  /* check if CM supports 1-1 text chat */
  connection = tp_account_get_connection (account);
  if (connection == NULL)
    {
      callback (FALSE, callback_data);
      return;
    }

  cb_data = g_slice_new0 (FilterCallbackData);
  cb_data->callback = callback;
  cb_data->user_data = callback_data;
  tp_proxy_prepare_async (connection, features, conn_prepared_cb, cb_data);
}

static void
empathy_new_message_dialog_update_sms_button_sensitivity (GtkWidget *widget,
    GParamSpec *pspec,
    GtkWidget *button)
{
  GtkWidget *self = gtk_widget_get_toplevel (widget);
  EmpathyContactSelectorDialog *dialog;
  TpConnection *conn;
  GPtrArray *rccs;
  gboolean sensitive = FALSE;
  guint i;

  g_return_if_fail (EMPATHY_IS_NEW_MESSAGE_DIALOG (self));

  dialog = EMPATHY_CONTACT_SELECTOR_DIALOG (self);

  /* if the Text widget isn't sensitive, don't bother checking the caps */
  if (!gtk_widget_get_sensitive (dialog->button_action))
    goto finally;

  empathy_contact_selector_dialog_get_selected (dialog, &conn, NULL);

  if (conn == NULL)
    goto finally;

  /* iterate the rccs to find if SMS channels are supported, this should
   * be in tp-glib */
  rccs = tp_capabilities_get_channel_classes (
      tp_connection_get_capabilities (conn));

  for (i = 0; i < rccs->len; i++)
    {
      GHashTable *fixed;
      GStrv allowed;
      const char *type;
      gboolean sms_channel;

      tp_value_array_unpack (g_ptr_array_index (rccs, i), 2,
          &fixed,
          &allowed);

      /* SMS channels are type:Text and sms-channel:True */
      type = tp_asv_get_string (fixed, TP_PROP_CHANNEL_CHANNEL_TYPE);
      sms_channel = tp_asv_get_boolean (fixed,
          TP_PROP_CHANNEL_INTERFACE_SMS_SMS_CHANNEL, NULL);

      sensitive = sms_channel &&
        !tp_strdiff (type, TP_IFACE_CHANNEL_TYPE_TEXT);

      if (sensitive)
        break;
    }

finally:
  gtk_widget_set_sensitive (button, sensitive);
}

static GObject *
empathy_new_message_dialog_constructor (GType type,
    guint n_props,
    GObjectConstructParam *props)
{
  GObject *retval;

  if (dialog_singleton)
    {
      retval = G_OBJECT (dialog_singleton);
      g_object_ref (retval);
    }
  else
    {
      retval = G_OBJECT_CLASS (
      empathy_new_message_dialog_parent_class)->constructor (type,
        n_props, props);

      dialog_singleton = EMPATHY_NEW_MESSAGE_DIALOG (retval);
      g_object_add_weak_pointer (retval, (gpointer) &dialog_singleton);
    }

  return retval;
}

static void
empathy_new_message_dialog_init (EmpathyNewMessageDialog *dialog)
{
  EmpathyContactSelectorDialog *parent = EMPATHY_CONTACT_SELECTOR_DIALOG (
        dialog);
  GtkWidget *button;
  GtkWidget *image;

  /* add an SMS button */
  button = gtk_button_new_with_mnemonic (_("_SMS"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_SMS,
      GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (button), image);

  gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button,
      EMP_NEW_MESSAGE_SMS);
  gtk_widget_show (button);

  /* add chat button */
  parent->button_action = gtk_button_new_with_mnemonic (_("C_hat"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_NEW_MESSAGE,
      GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (parent->button_action), image);

  gtk_dialog_add_action_widget (GTK_DIALOG (dialog), parent->button_action,
      EMP_NEW_MESSAGE_TEXT);
  gtk_widget_show (parent->button_action);

  /* the parent class will update the sensitivity of button_action, propagate
   * it */
  g_signal_connect (parent->button_action, "notify::sensitive",
      G_CALLBACK (empathy_new_message_dialog_update_sms_button_sensitivity),
      button);
  g_signal_connect (dialog, "notify::selected-account",
      G_CALLBACK (empathy_new_message_dialog_update_sms_button_sensitivity),
      button);

  /* Tweak the dialog */
  gtk_window_set_title (GTK_WINDOW (dialog), _("New Conversation"));
  gtk_window_set_role (GTK_WINDOW (dialog), "new_message");

  gtk_widget_set_sensitive (parent->button_action, FALSE);
}

static void
empathy_new_message_dialog_class_init (
  EmpathyNewMessageDialogClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GtkDialogClass *dialog_class = GTK_DIALOG_CLASS (class);
  EmpathyContactSelectorDialogClass *selector_dialog_class = \
    EMPATHY_CONTACT_SELECTOR_DIALOG_CLASS (class);

  object_class->constructor = empathy_new_message_dialog_constructor;

  dialog_class->response = empathy_new_message_dialog_response;

  selector_dialog_class->account_filter = empathy_new_message_account_filter;
}

/**
 * empathy_new_message_dialog_new:
 * @parent: parent #GtkWindow of the dialog
 *
 * Create a new #EmpathyNewMessageDialog it.
 *
 * Return value: the new #EmpathyNewMessageDialog
 */
GtkWidget *
empathy_new_message_dialog_show (GtkWindow *parent)
{
  GtkWidget *dialog;

  dialog = g_object_new (EMPATHY_TYPE_NEW_MESSAGE_DIALOG, NULL);

  if (parent)
    {
      gtk_window_set_transient_for (GTK_WINDOW (dialog),
          GTK_WINDOW (parent));
    }

  gtk_widget_show (dialog);
  return dialog;
}
