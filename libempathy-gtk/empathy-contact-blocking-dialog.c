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
#include "config.h"

#include <glib/gi18n-lib.h>

#include <libempathy/empathy-utils.h>

#include <libempathy/empathy-contact-manager.h>
#include <libempathy/empathy-tp-contact-list.h>

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
  /* a map of all active connections to their 'deny' channel */
  GHashTable *channels; /* reffed TpConnection* -> reffed TpChannel* */

  guint block_account_changed;

  GtkListStore *blocked_contacts;
  GtkListStore *completion_contacts;
  GtkTreeSelection *selection;

  GtkWidget *account_chooser;
  GtkWidget *add_button;
  GtkWidget *add_contact_entry;
  GtkWidget *info_bar;
  GtkWidget *info_bar_label;
  GtkWidget *remove_button;
};

enum /* blocked-contacts columns */
{
  COL_BLOCKED_IDENTIFIER,
  COL_BLOCKED_HANDLE,
  N_BLOCKED_COLUMNS
};

enum /* completion_contacts columns */
{
  COL_COMPLETION_IDENTIFIER,
  COL_COMPLETION_TEXT,
  N_COMPLETION_COLUMNS
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

static void contact_blocking_dialog_account_changed (GtkWidget *,
    EmpathyContactBlockingDialog *);

static void
contact_blocking_dialog_refilter_account_chooser (
    EmpathyContactBlockingDialog *self)
{
  EmpathyAccountChooser *chooser =
    EMPATHY_ACCOUNT_CHOOSER (self->priv->account_chooser);
  TpConnection *conn;
  gboolean enabled;

  DEBUG ("Refiltering account chooser");

  /* set the filter to refilter the account chooser */
  self->priv->block_account_changed++;
  empathy_account_chooser_set_filter (chooser,
      contact_blocking_dialog_filter_account_chooser, self);
  self->priv->block_account_changed--;

  conn = empathy_account_chooser_get_connection (chooser);
  enabled = (empathy_account_chooser_get_account (chooser) != NULL &&
             conn != NULL &&
             g_hash_table_lookup (self->priv->channels, conn) != NULL);

  if (!enabled)
    DEBUG ("No account selected");

  gtk_widget_set_sensitive (self->priv->add_button, enabled);
  gtk_widget_set_sensitive (self->priv->add_contact_entry, enabled);

  contact_blocking_dialog_account_changed (self->priv->account_chooser, self);
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
          COL_BLOCKED_IDENTIFIER, identifier,
          COL_BLOCKED_HANDLE, handle,
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
        contact_blocking_dialog_refilter_account_chooser (self);
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

  DEBUG ("deny list changed on %s: %u added, %u removed",
      get_pretty_conn_name (conn), added->len, removed->len);

  /* add contacts */
  contact_blocking_dialog_add_contacts_to_list (self, conn, added);

  /* remove contacts */
  removed_set = tp_intset_from_array (removed);

  valid = gtk_tree_model_get_iter_first (model, &iter);
  while (valid)
    {
      TpHandle handle;

      gtk_tree_model_get (model, &iter,
          COL_BLOCKED_HANDLE, &handle,
          -1);

      if (tp_intset_is_member (removed_set, handle))
        valid = gtk_list_store_remove (self->priv->blocked_contacts, &iter);
      else
        valid = gtk_tree_model_iter_next (model, &iter);
    }

  tp_intset_destroy (removed_set);
}

DECLARE_CALLBACK (contact_blocking_dialog_connection_prepared);

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
      TpConnection *conn;

      tp_g_signal_connect_object (account, "status-changed",
          G_CALLBACK (contact_blocking_dialog_connection_status_changed),
          self, 0);

      conn = tp_account_get_connection (TP_ACCOUNT (account));

      if (conn != NULL)
        {
          tp_proxy_prepare_async (conn, NULL,
              contact_blocking_dialog_connection_prepared, self);
        }
    }

  g_list_free (accounts);
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
      DEBUG ("Failed to prepare connection %s: %s",
          get_pretty_conn_name ((TpConnection *) conn), error->message);
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
      DEBUG ("Failed to get 'deny' channel on %s: %s",
          get_pretty_conn_name (conn), in_error->message);
      return;
    }

  channel = tp_channel_new_from_properties (conn, channel_path, props, &error);

  if (error != NULL)
    {
      DEBUG ("Failed to create channel proxy on %s: %s",
          get_pretty_conn_name (conn), in_error->message);
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
      DEBUG ("Failed to prepare channel %s: %s",
          tp_proxy_get_object_path (channel), error->message);
      g_error_free (error);
      return;
    }

  conn = tp_channel_borrow_connection (TP_CHANNEL (channel));

  DEBUG ("Channel %s prepared for connection %s",
      tp_proxy_get_object_path (channel), get_pretty_conn_name (conn));

  g_hash_table_insert (self->priv->channels,
      g_object_ref (conn), channel);
  contact_blocking_dialog_refilter_account_chooser (self);

  tp_g_signal_connect_object (channel, "group-members-changed",
      G_CALLBACK (contact_blocking_dialog_deny_channel_members_changed),
      self, 0);
}

static void
contact_blocking_dialog_set_error (EmpathyContactBlockingDialog *self,
    const GError *error)
{
  const char *msg = NULL;

  if (error->domain == TP_ERRORS)
    {
      if (error->code == TP_ERROR_INVALID_HANDLE)
        msg = _("Unknown or invalid identifier");
      else if (error->code == TP_ERROR_NOT_AVAILABLE)
        msg = _("Contact blocking temporarily unavailable");
      else if (error->code == TP_ERROR_NOT_CAPABLE)
        msg = _("Contact blocking unavailable");
      else if (error->code == TP_ERROR_PERMISSION_DENIED)
        msg = _("Permission Denied");
    }

  if (msg == NULL)
    msg = _("Could not block contact");

  gtk_label_set_text (GTK_LABEL (self->priv->info_bar_label), msg);
  gtk_widget_show (self->priv->info_bar);
}

static void contact_blocking_dialog_add_contact_got_handle (TpConnection *,
    const GArray *, const GError *, gpointer, GObject *);

static void
contact_blocking_dialog_add_contact (GtkWidget *widget,
    EmpathyContactBlockingDialog *self)
{
  TpConnection *conn = empathy_account_chooser_get_connection (
      EMPATHY_ACCOUNT_CHOOSER (self->priv->account_chooser));
  const char *identifiers[2] = { NULL, };

  identifiers[0] = gtk_entry_get_text (
      GTK_ENTRY (self->priv->add_contact_entry));

  DEBUG ("Looking up handle for '%s' on %s",
      identifiers[0], get_pretty_conn_name (conn));

  tp_cli_connection_call_request_handles (conn, -1,
      TP_HANDLE_TYPE_CONTACT, identifiers,
      contact_blocking_dialog_add_contact_got_handle,
      NULL, NULL, G_OBJECT (self));

  gtk_entry_set_text (GTK_ENTRY (self->priv->add_contact_entry), "");
  gtk_widget_hide (self->priv->info_bar);
}

static void
contact_blocking_dialog_added_contact (TpChannel *, const GError *,
    gpointer, GObject *);

static void
contact_blocking_dialog_add_contact_got_handle (TpConnection *conn,
    const GArray *handles,
    const GError *in_error,
    gpointer user_data,
    GObject *self)
{
  EmpathyContactBlockingDialogPrivate *priv = GET_PRIVATE (self);
  TpChannel *channel = g_hash_table_lookup (priv->channels, conn);

  if (in_error != NULL)
    {
      DEBUG ("Error getting handle on %s: %s",
          get_pretty_conn_name (conn), in_error->message);

      contact_blocking_dialog_set_error (
          EMPATHY_CONTACT_BLOCKING_DIALOG (self), in_error);

      return;
    }

  g_return_if_fail (handles->len == 1);

  DEBUG ("Adding handle %u to deny channel on %s",
      g_array_index (handles, TpHandle, 0), get_pretty_conn_name (conn));

  tp_cli_channel_interface_group_call_add_members (channel, -1,
      handles, "",
      contact_blocking_dialog_added_contact, NULL, NULL, self);
}

static void
contact_blocking_dialog_added_contact (TpChannel *channel,
    const GError *in_error,
    gpointer user_data,
    GObject *self)
{
  if (in_error != NULL)
    {
      DEBUG ("Error adding contact to deny list %s: %s",
          tp_proxy_get_object_path (channel), in_error->message);

      contact_blocking_dialog_set_error (
          EMPATHY_CONTACT_BLOCKING_DIALOG (self), in_error);

      return;
    }

  DEBUG ("Contact added to %s", tp_proxy_get_object_path (channel));
}

static void
contact_blocking_dialog_removed_contacts (TpChannel *,
    const GError *, gpointer, GObject *);

static void
contact_blocking_dialog_remove_contacts (GtkWidget *button,
    EmpathyContactBlockingDialog *self)
{
  TpConnection *conn = empathy_account_chooser_get_connection (
      EMPATHY_ACCOUNT_CHOOSER (self->priv->account_chooser));
  TpChannel *channel = g_hash_table_lookup (self->priv->channels, conn);
  GtkTreeModel *model;
  GList *rows, *ptr;
  GArray *handles = g_array_new (FALSE, FALSE, sizeof (TpHandle));

  rows = gtk_tree_selection_get_selected_rows (self->priv->selection, &model);

  for (ptr = rows; ptr != NULL; ptr = ptr->next)
    {
      GtkTreePath *path = ptr->data;
      GtkTreeIter iter;
      TpHandle handle;

      if (!gtk_tree_model_get_iter (model, &iter, path))
        continue;

      gtk_tree_model_get (model, &iter,
          COL_BLOCKED_HANDLE, &handle,
          -1);

      g_array_append_val (handles, handle);
      gtk_tree_path_free (path);
    }

  g_list_free (rows);

  if (handles->len > 0)
    {
      DEBUG ("Removing %u handles", handles->len);

      tp_cli_channel_interface_group_call_remove_members (channel, -1,
          handles, "",
          contact_blocking_dialog_removed_contacts,
          NULL, NULL, G_OBJECT (self));
    }

  g_array_unref (handles);
}

static void
contact_blocking_dialog_removed_contacts (TpChannel *channel,
    const GError *in_error,
    gpointer user_data,
    GObject *self)
{
  if (in_error != NULL)
    {
      DEBUG ("Error removing contacts from deny list: %s", in_error->message);

      contact_blocking_dialog_set_error (
          EMPATHY_CONTACT_BLOCKING_DIALOG (self), in_error);

      return;
    }

  DEBUG ("Contacts removed");
}

static void
contact_blocking_dialog_account_changed (GtkWidget *account_chooser,
    EmpathyContactBlockingDialog *self)
{
  TpConnection *conn = empathy_account_chooser_get_connection (
      EMPATHY_ACCOUNT_CHOOSER (account_chooser));
  TpChannel *channel;
  GArray *blocked;
  EmpathyContactManager *contact_manager;
  EmpathyTpContactList *contact_list;
  GList *members, *ptr;

  if (self->priv->block_account_changed > 0)
    return;

  /* clear the lists of contacts */
  gtk_list_store_clear (self->priv->blocked_contacts);
  gtk_list_store_clear (self->priv->completion_contacts);

  if (conn == NULL)
    return;

  DEBUG ("Account changed: %s", get_pretty_conn_name (conn));

  /* load the deny list */
  channel = g_hash_table_lookup (self->priv->channels, conn);

  if (channel == NULL)
    return;

  g_return_if_fail (TP_IS_CHANNEL (channel));

  blocked = tp_intset_to_array (tp_channel_group_get_members (channel));

  DEBUG ("%u contacts on blocked list", blocked->len);

  contact_blocking_dialog_add_contacts_to_list (self, conn, blocked);
  g_array_unref (blocked);

  /* load the completion list */
  g_return_if_fail (empathy_contact_manager_initialized ());

  DEBUG ("Loading contacts");

  contact_manager = empathy_contact_manager_dup_singleton ();
  contact_list = empathy_contact_manager_get_list (contact_manager, conn);
  members = empathy_contact_list_get_members (
      EMPATHY_CONTACT_LIST (contact_list));

  for (ptr = members; ptr != NULL; ptr = ptr->next)
    {
      EmpathyContact *contact = ptr->data;
      gchar *tmpstr;

      tmpstr = g_strdup_printf ("%s (%s)",
          empathy_contact_get_alias (contact),
          empathy_contact_get_id (contact));

      gtk_list_store_insert_with_values (self->priv->completion_contacts,
          NULL, -1,
          COL_COMPLETION_IDENTIFIER, empathy_contact_get_id (contact),
          COL_COMPLETION_TEXT, tmpstr,
          -1);

      g_free (tmpstr);
      g_object_unref (contact);
    }

  g_list_free (members);
  g_object_unref (contact_manager);
}

static void
contact_blocking_dialog_view_selection_changed (GtkTreeSelection *selection,
    EmpathyContactBlockingDialog *self)
{
  GList *rows = gtk_tree_selection_get_selected_rows (selection, NULL);

  /* update the sensitivity of the remove button */
  gtk_widget_set_sensitive (self->priv->remove_button, rows != NULL);

  g_list_foreach (rows, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (rows);
}

static gboolean
contact_selector_dialog_match_func (GtkEntryCompletion *completion,
    const gchar *key,
    GtkTreeIter *iter,
    gpointer user_data)
{
  GtkTreeModel *model;
  gchar *str, *lower;
  gboolean v = FALSE;

  model = gtk_entry_completion_get_model (completion);
  if (model == NULL || iter == NULL)
    return FALSE;

  gtk_tree_model_get (model, iter, COL_COMPLETION_TEXT, &str, -1);
  lower = g_utf8_strdown (str, -1);
  if (strstr (lower, key))
    {
      DEBUG ("Key %s is matching name **%s**", key, str);
      v = TRUE;
      goto out;
    }
  g_free (str);
  g_free (lower);

  gtk_tree_model_get (model, iter, COL_COMPLETION_IDENTIFIER, &str, -1);
  lower = g_utf8_strdown (str, -1);
  if (strstr (lower, key))
    {
      DEBUG ("Key %s is matching ID **%s**", key, str);
      v = TRUE;
      goto out;
    }

out:
  g_free (str);
  g_free (lower);

  return v;
}

static gboolean
contact_selector_dialog_match_selected_cb (GtkEntryCompletion *widget,
    GtkTreeModel *model,
    GtkTreeIter *iter,
    EmpathyContactBlockingDialog *self)
{
  gchar *id;

  if (iter == NULL || model == NULL)
    return FALSE;

  gtk_tree_model_get (model, iter, COL_COMPLETION_IDENTIFIER, &id, -1);
  gtk_entry_set_text (GTK_ENTRY (self->priv->add_contact_entry), id);

  DEBUG ("Got selected match **%s**", id);

  g_free (id);

  return TRUE;
}

static void
contact_blocking_dialog_dispose (GObject *self)
{
  EmpathyContactBlockingDialogPrivate *priv = GET_PRIVATE (self);

  tp_clear_pointer (&priv->channels, g_hash_table_destroy);

  G_OBJECT_CLASS (empathy_contact_blocking_dialog_parent_class)->dispose (self);
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
  GtkWidget *account_hbox, *blocked_contacts_view, *blocked_contacts_sw,
      *remove_toolbar;
  GtkEntryCompletion *completion;
  TpAccountManager *am;
  GtkStyleContext *context;

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
      "add-button", &self->priv->add_button,
      "add-contact-entry", &self->priv->add_contact_entry,
      "blocked-contacts", &self->priv->blocked_contacts,
      "blocked-contacts-sw", &blocked_contacts_sw,
      "blocked-contacts-view", &blocked_contacts_view,
      "remove-button", &self->priv->remove_button,
      "remove-toolbar", &remove_toolbar,
      NULL);

  empathy_builder_connect (gui, self,
      "add-button", "clicked", contact_blocking_dialog_add_contact,
      "add-contact-entry", "activate", contact_blocking_dialog_add_contact,
      "remove-button", "clicked", contact_blocking_dialog_remove_contacts,
      NULL);

  /* join the remove toolbar to the treeview */
  context = gtk_widget_get_style_context (blocked_contacts_sw);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);
  context = gtk_widget_get_style_context (remove_toolbar);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

  /* add the contents to the dialog */
  gtk_container_add (
      GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (self))),
      contents);
  gtk_widget_show (contents);

  /* set up the tree selection */
  self->priv->selection = gtk_tree_view_get_selection (
      GTK_TREE_VIEW (blocked_contacts_view));
  gtk_tree_selection_set_mode (self->priv->selection, GTK_SELECTION_MULTIPLE);
  g_signal_connect (self->priv->selection, "changed",
      G_CALLBACK (contact_blocking_dialog_view_selection_changed), self);

  /* build the contact entry */
  self->priv->completion_contacts = gtk_list_store_new (N_COMPLETION_COLUMNS,
      G_TYPE_STRING, /* id */
      G_TYPE_UINT, /* handle */
      G_TYPE_STRING); /* text */
  completion = gtk_entry_completion_new ();
  gtk_entry_completion_set_model (completion,
      GTK_TREE_MODEL (self->priv->completion_contacts));
  gtk_entry_completion_set_text_column (completion, COL_COMPLETION_TEXT);
  gtk_entry_completion_set_match_func (completion,
      contact_selector_dialog_match_func,
      NULL, NULL);
  g_signal_connect (completion, "match-selected",
        G_CALLBACK (contact_selector_dialog_match_selected_cb),
        self);
  gtk_entry_set_completion (GTK_ENTRY (self->priv->add_contact_entry),
      completion);
  g_object_unref (completion);
  g_object_unref (self->priv->completion_contacts);

  /* add the account chooser */
  self->priv->account_chooser = empathy_account_chooser_new ();
  contact_blocking_dialog_refilter_account_chooser (self);
  g_signal_connect (self->priv->account_chooser, "changed",
      G_CALLBACK (contact_blocking_dialog_account_changed), self);

  gtk_box_pack_start (GTK_BOX (account_hbox), self->priv->account_chooser,
      TRUE, TRUE, 0);
  gtk_widget_show (self->priv->account_chooser);

  /* add an error warning info bar */
  self->priv->info_bar = gtk_info_bar_new ();
  gtk_box_pack_start (GTK_BOX (contents), self->priv->info_bar, FALSE, TRUE, 0);
  gtk_info_bar_set_message_type (GTK_INFO_BAR (self->priv->info_bar),
      GTK_MESSAGE_ERROR);

  self->priv->info_bar_label = gtk_label_new ("");
  gtk_container_add (GTK_CONTAINER (
        gtk_info_bar_get_content_area (GTK_INFO_BAR (self->priv->info_bar))),
      self->priv->info_bar_label);
  gtk_widget_show (self->priv->info_bar_label);

  /* prepare the account manager */
  am = tp_account_manager_dup ();
  tp_proxy_prepare_async (am, NULL, contact_blocking_dialog_am_prepared, self);
  g_object_unref (am);

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
