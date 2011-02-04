/*
 * empathy-contact-blocking-dialog.c
 *
 * EmpathyContactBlockingDialog
 *
 * Copyright (C) 2011 Collabora Ltd.
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
 * Authors: Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#include <glib/gi18n.h>

#include <libempathy/empathy-utils.h>

#include <libempathy-gtk/empathy-account-chooser.h>
#include <libempathy-gtk/empathy-ui-utils.h>

#include "empathy-contact-blocking-dialog.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

#define GET_PRIVATE(o) (EMPATHY_CONTACT_BLOCKING_DIALOG (o)->priv)
#define DECLARE_CALLBACK(func) \
  static void func (GObject *, GAsyncResult *, gpointer);

G_DEFINE_TYPE (EmpathyContactBlockingDialog, empathy_contact_blocking_dialog,
    GTK_TYPE_DIALOG);

struct _EmpathyContactBlockingDialogPrivate
{
  GHashTable *channels; /* TpConnection* -> TpChannel* */
  GtkListStore *blocked_contacts;

  GtkWidget *account_chooser;
};

enum /* blocked-contacts columns */
{
  COL_IDENTIFIER,
  COL_HANDLE,
  N_COLUMNS
};

static const char *
get_pretty_conn_name (TpConnection *conn)
{
  return tp_proxy_get_object_path (conn) + strlen (TP_CONN_OBJECT_PATH_BASE);
}

static void
contact_blocking_dialog_filter_account_chooser (TpAccount *account,
    EmpathyAccountChooserFilterResultCallback callback,
    gpointer callback_data,
    gpointer user_data)
{
  EmpathyContactBlockingDialog *self = user_data;
  TpConnection *conn = tp_account_get_connection (account);
  gboolean enable;

  enable =
    conn != NULL &&
    g_hash_table_lookup (self->priv->channels, conn) != NULL;

  callback (enable, callback_data);
}

static void contact_blocking_dialog_inspected_handles (TpConnection *,
    const char **, const GError *, gpointer, GObject *);

static void
contact_blocking_dialog_add_contacts_to_list (
    EmpathyContactBlockingDialog *self,
    TpConnection *conn,
    GArray *handles)
{
  if (handles->len > 0)
    tp_cli_connection_call_inspect_handles (conn, -1,
        TP_HANDLE_TYPE_CONTACT, handles,
        contact_blocking_dialog_inspected_handles,
        g_boxed_copy (DBUS_TYPE_G_UINT_ARRAY, handles),
        (GDestroyNotify) g_array_unref, G_OBJECT (self));
}

static void
contact_blocking_dialog_inspected_handles (TpConnection *conn,
    const char **identifiers,
    const GError *in_error,
    gpointer user_data,
    GObject *self)
{
  EmpathyContactBlockingDialogPrivate *priv = GET_PRIVATE (self);
  GArray *handles = user_data;
  guint i;

  if (in_error != NULL)
    {
      DEBUG ("Failed to inspect handles: %s", in_error->message);
      return;
    }

  DEBUG ("Adding %u identifiers", handles->len);

  for (i = 0; i < handles->len; i++)
    {
      const char *identifier = identifiers[i];
      TpHandle handle = g_array_index (handles, TpHandle, i);

      gtk_list_store_insert_with_values (priv->blocked_contacts, NULL, -1,
          COL_IDENTIFIER, identifier,
          COL_HANDLE, handle,
          -1);
    }
}

DECLARE_CALLBACK (contact_blocking_dialog_connection_prepared);

static void
contact_blocking_dialog_connection_status_changed (TpAccount *account,
    guint old_status,
    guint new_status,
    guint reason,
    const char *dbus_reason,
    GHashTable *details,
    EmpathyContactBlockingDialog *self)
{
  TpConnection *conn = tp_account_get_connection (account);

  switch (new_status)
    {
      case TP_CONNECTION_STATUS_DISCONNECTED:
        DEBUG ("Connection %s invalidated", get_pretty_conn_name (conn));

        /* remove the channel from the hash table */
        g_hash_table_remove (self->priv->channels, conn);

        /* set the filter again to refilter the account list */
        empathy_account_chooser_set_filter (
            EMPATHY_ACCOUNT_CHOOSER (self->priv->account_chooser),
            contact_blocking_dialog_filter_account_chooser, self);
        break;

      case TP_CONNECTION_STATUS_CONNECTING:
        break;

      case TP_CONNECTION_STATUS_CONNECTED:
        DEBUG ("Connection %s reconnected", get_pretty_conn_name (conn));

        tp_proxy_prepare_async (conn, NULL,
            contact_blocking_dialog_connection_prepared, self);
    }
}

static void
contact_blocking_dialog_deny_channel_members_changed (TpChannel *channel,
    const char *message,
    GArray *added,
    GArray *removed,
    GArray *local_pending,
    GArray *remote_pending,
    TpHandle actor,
    guint reason,
    EmpathyContactBlockingDialog *self)
{
  TpConnection *conn = tp_channel_borrow_connection (channel);
  GtkTreeModel *model = GTK_TREE_MODEL (self->priv->blocked_contacts);
  GtkTreeIter iter;
  TpIntset *removed_set;
  gboolean valid;

  /* we only care about changes to the selected connection */
  /* FIXME: can we compare proxy pointers directly? */
  if (tp_strdiff (
        tp_proxy_get_object_path (tp_channel_borrow_connection (channel)),
        tp_proxy_get_object_path (empathy_account_chooser_get_connection (
            EMPATHY_ACCOUNT_CHOOSER (self->priv->account_chooser)))))
    return;

  DEBUG ("deny list changed: %u added, %u removed", added->len, removed->len);

  /* add contacts */
  contact_blocking_dialog_add_contacts_to_list (self, conn, added);

  /* remove contacts */
  removed_set = tp_intset_from_array (removed);

  valid = gtk_tree_model_get_iter_first (model, &iter);
  while (valid)
    {
      TpHandle handle;

      gtk_tree_model_get (model, &iter,
          COL_HANDLE, &handle,
          -1);

      if (tp_intset_is_member (removed_set, handle))
        valid = gtk_list_store_remove (self->priv->blocked_contacts, &iter);
      else
        valid = gtk_tree_model_iter_next (model, &iter);
    }

  tp_intset_destroy (removed_set);
}

DECLARE_CALLBACK (contact_blocking_dialog_account_prepared);

static void
contact_blocking_dialog_am_prepared (GObject *am,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyContactBlockingDialog *self = user_data;
  GList *accounts, *ptr;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (am, result, &error))
    {
      g_critical ("Could not prepare Account Manager: %s", error->message);
      g_error_free (error);
      return;
    }

  accounts = tp_account_manager_get_valid_accounts (TP_ACCOUNT_MANAGER (am));

  for (ptr = accounts; ptr != NULL; ptr = ptr->next)
    {
      TpAccount *account = ptr->data;

      tp_proxy_prepare_async (account, NULL,
          contact_blocking_dialog_account_prepared, self);
    }

  g_list_free (accounts);
}

static void
contact_blocking_dialog_account_prepared (GObject *account,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyContactBlockingDialog *self = user_data;
  TpConnection *conn;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (account, result, &error))
    {
      DEBUG ("Could not prepare Account: %s", error->message);
      g_error_free (error);
      return;
    }

  g_signal_connect (account, "status-changed",
      G_CALLBACK (contact_blocking_dialog_connection_status_changed), self);

  conn = tp_account_get_connection (TP_ACCOUNT (account));

  if (conn != NULL)
    {
      tp_proxy_prepare_async (conn, NULL,
          contact_blocking_dialog_connection_prepared, self);
    }
}

static void contact_blocking_dialog_got_deny_channel (TpConnection *,
    gboolean, const char *, GHashTable *, const GError *, gpointer, GObject *);

static void
contact_blocking_dialog_connection_prepared (GObject *conn,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyContactBlockingDialog *self = user_data;
  GHashTable *request;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (conn, result, &error))
    {
      DEBUG ("Failed to prepare connection: %s", error->message);
      g_error_free (error);
      return;
    }

  /* request the deny channel */
  request = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE,
      G_TYPE_STRING,
      TP_IFACE_CHANNEL_TYPE_CONTACT_LIST,

      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
      G_TYPE_UINT,
      TP_HANDLE_TYPE_LIST,

      TP_PROP_CHANNEL_TARGET_ID,
      G_TYPE_STRING,
      "deny",

      NULL);

  tp_cli_connection_interface_requests_call_ensure_channel (
      TP_CONNECTION (conn), -1, request,
      contact_blocking_dialog_got_deny_channel, NULL, NULL, G_OBJECT (self));

  g_hash_table_destroy (request);
}

DECLARE_CALLBACK (contact_blocking_dialog_deny_channel_prepared);

static void
contact_blocking_dialog_got_deny_channel (TpConnection *conn,
    gboolean yours,
    const char *channel_path,
    GHashTable *props,
    const GError *in_error,
    gpointer user_data,
    GObject *self)
{
  TpChannel *channel;
  GError *error = NULL;

  const GQuark features[] = {
      TP_CHANNEL_FEATURE_CORE,
      TP_CHANNEL_FEATURE_GROUP,
      0 };

  if (in_error != NULL)
    {
      DEBUG ("Failed to get 'deny' channel: %s", in_error->message);
      return;
    }

  channel = tp_channel_new_from_properties (conn, channel_path, props, &error);

  if (error != NULL)
    {
      DEBUG ("Failed to create channel proxy: %s", error->message);
      g_error_free (error);
      return;
    }

  tp_proxy_prepare_async (channel, features,
      contact_blocking_dialog_deny_channel_prepared, self);
}

static void
contact_blocking_dialog_deny_channel_prepared (GObject *channel,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyContactBlockingDialog *self = user_data;
  TpConnection *conn;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (channel, result, &error))
    {
      DEBUG ("Failed to prepare channel: %s", error->message);
      g_error_free (error);
      return;
    }

  conn = tp_channel_borrow_connection (TP_CHANNEL (channel));

  DEBUG ("Channel prepared for connection %s", get_pretty_conn_name (conn));

  g_hash_table_insert (self->priv->channels,
      g_object_ref (conn), channel);

  /* set the filter again to refilter the account list */
  empathy_account_chooser_set_filter (
      EMPATHY_ACCOUNT_CHOOSER (self->priv->account_chooser),
      contact_blocking_dialog_filter_account_chooser, self);

  g_signal_connect (channel, "group-members-changed",
      G_CALLBACK (contact_blocking_dialog_deny_channel_members_changed), self);
}

static void
contact_blocking_dialog_account_changed (GtkWidget *account_chooser,
    EmpathyContactBlockingDialog *self)
{
  TpConnection *conn = empathy_account_chooser_get_connection (
      EMPATHY_ACCOUNT_CHOOSER (account_chooser));
  TpChannel *channel;
  GArray *blocked;

  if (conn == NULL)
    return;

  DEBUG ("Account changed: %s", get_pretty_conn_name (conn));

  /* FIXME: clear the completion, get the new blocked list */

  /* clear the list of blocked contacts */
  gtk_list_store_clear (self->priv->blocked_contacts);

  /* load the deny list */
  channel = g_hash_table_lookup (self->priv->channels, conn);

  g_return_if_fail (TP_IS_CHANNEL (channel));

  blocked = tp_intset_to_array (tp_channel_group_get_members (channel));

  DEBUG ("%u contacts on blocked list", blocked->len);

  contact_blocking_dialog_add_contacts_to_list (self, conn, blocked);

  g_array_unref (blocked);
}

static void
contact_blocking_dialog_dispose (GObject *self)
{
  EmpathyContactBlockingDialogPrivate *priv = GET_PRIVATE (self);

  tp_clear_pointer (&priv->channels, g_hash_table_destroy);
}

static void
empathy_contact_blocking_dialog_class_init (
    EmpathyContactBlockingDialogClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = contact_blocking_dialog_dispose;

  g_type_class_add_private (gobject_class,
      sizeof (EmpathyContactBlockingDialogPrivate));
}

static void
empathy_contact_blocking_dialog_init (EmpathyContactBlockingDialog *self)
{
  GtkBuilder *gui;
  char *filename;
  GtkWidget *contents;
  GtkWidget *account_hbox, *add_contact_entry;
  TpAccountManager *am;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_CONTACT_BLOCKING_DIALOG,
      EmpathyContactBlockingDialogPrivate);

  self->priv->channels = g_hash_table_new_full (NULL, NULL,
      g_object_unref, g_object_unref);

  gtk_window_set_title (GTK_WINDOW (self), _("Edit Blocked Contacts"));
  gtk_dialog_add_button (GTK_DIALOG (self),
      GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);

  filename = empathy_file_lookup ("empathy-contact-blocking-dialog.ui",
      "libempathy-gtk");

  gui = empathy_builder_get_file (filename,
      "contents", &contents,
      "account-hbox", &account_hbox,
      "add-contact-entry", &add_contact_entry,
      "blocked-contacts", &self->priv->blocked_contacts,
      NULL);

  /* add the contents to the dialog */
  gtk_container_add (
      GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (self))),
      contents);
  gtk_widget_show (contents);

  /* add the account chooser */
  self->priv->account_chooser = empathy_account_chooser_new ();
  empathy_account_chooser_set_filter (
      EMPATHY_ACCOUNT_CHOOSER (self->priv->account_chooser),
      contact_blocking_dialog_filter_account_chooser, self);
  g_signal_connect (self->priv->account_chooser, "changed",
      G_CALLBACK (contact_blocking_dialog_account_changed), self);

  gtk_box_pack_start (GTK_BOX (account_hbox), self->priv->account_chooser,
      TRUE, TRUE, 0);
  gtk_widget_show (self->priv->account_chooser);

  /* build the contact entry */
  // FIXME

  /* prepare the account manager */
  am = tp_account_manager_dup ();
  tp_proxy_prepare_async (am, NULL, contact_blocking_dialog_am_prepared, self);

  g_free (filename);
  g_object_unref (gui);
}

GtkWidget *
empathy_contact_blocking_dialog_new (GtkWindow *parent)
{
  GtkWidget *self = g_object_new (EMPATHY_TYPE_CONTACT_BLOCKING_DIALOG,
      NULL);

  if (parent != NULL)
    {
      gtk_window_set_transient_for (GTK_WINDOW (self), parent);
    }

  return self;
}
