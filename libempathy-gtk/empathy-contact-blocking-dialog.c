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

  TpConnection *current_conn;
};

enum /* blocked-contacts columns */
{
  COL_BLOCKED_IDENTIFIER,
  COL_BLOCKED_CONTACT,
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
  TpConnection *conn = tp_account_get_connection (account);
  gboolean enable;

  enable =
    conn != NULL &&
    tp_proxy_has_interface_by_id (conn,
      TP_IFACE_QUARK_CONNECTION_INTERFACE_CONTACT_BLOCKING);

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
             tp_proxy_has_interface_by_id (conn,
               TP_IFACE_QUARK_CONNECTION_INTERFACE_CONTACT_BLOCKING));

  if (!enabled)
    DEBUG ("No account selected");

  gtk_widget_set_sensitive (self->priv->add_button, enabled);
  gtk_widget_set_sensitive (self->priv->add_contact_entry, enabled);

  contact_blocking_dialog_account_changed (self->priv->account_chooser, self);
}

static void
contact_blocking_dialog_add_blocked (
    EmpathyContactBlockingDialog *self,
    GPtrArray *blocked)
{
  EmpathyContactBlockingDialogPrivate *priv = GET_PRIVATE (self);
  guint i;

  if (blocked == NULL)
    return;

  for (i = 0; i < blocked->len; i++)
    {
      TpContact *contact = g_ptr_array_index (blocked, i);

      gtk_list_store_insert_with_values (priv->blocked_contacts, NULL, -1,
          COL_BLOCKED_IDENTIFIER, tp_contact_get_identifier (contact),
          COL_BLOCKED_CONTACT, contact,
          -1);
    }
}

static void
blocked_contacts_changed_cb (TpConnection *conn,
    GPtrArray *added,
    GPtrArray *removed,
    EmpathyContactBlockingDialog *self)
{
  GtkTreeModel *model = GTK_TREE_MODEL (self->priv->blocked_contacts);
  GtkTreeIter iter;
  gboolean valid;

  DEBUG ("blocked contacts changed on %s: %u added, %u removed",
      get_pretty_conn_name (conn), added->len, removed->len);

  /* add contacts */
  contact_blocking_dialog_add_blocked (self, added);

  /* remove contacts */
  valid = gtk_tree_model_get_iter_first (model, &iter);
  while (valid)
    {
      TpContact *contact;

      gtk_tree_model_get (model, &iter,
          COL_BLOCKED_CONTACT, &contact,
          -1);

      if (tp_g_ptr_array_contains (removed, contact))
        valid = gtk_list_store_remove (self->priv->blocked_contacts, &iter);
      else
        valid = gtk_tree_model_iter_next (model, &iter);

      g_object_unref (contact);
    }
}

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

        contact_blocking_dialog_refilter_account_chooser (self);
        break;

      case TP_CONNECTION_STATUS_CONNECTING:
        break;

      case TP_CONNECTION_STATUS_CONNECTED:
        DEBUG ("Connection %s reconnected", get_pretty_conn_name (conn));

        contact_blocking_dialog_refilter_account_chooser (self);
    }
}

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

      tp_g_signal_connect_object (account, "status-changed",
          G_CALLBACK (contact_blocking_dialog_connection_status_changed),
          self, 0);

      contact_blocking_dialog_refilter_account_chooser (self);
    }

  g_list_free (accounts);
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

static void
block_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyContactBlockingDialog *self = user_data;
  GError *error = NULL;

  if (!tp_contact_block_finish (TP_CONTACT (source), result,
        &error))
    {
      DEBUG ("Error blocking contacts: %s", error->message);

      contact_blocking_dialog_set_error (
          EMPATHY_CONTACT_BLOCKING_DIALOG (self), error);

      g_error_free (error);
      return;
    }

  DEBUG ("Contact blocked");
}

static void
block_contact_got_contact(TpConnection *conn,
    guint n_contacts,
    TpContact * const *contacts,
    const gchar * const *requested_ids,
    GHashTable *failed_id_errors,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyContactBlockingDialog *self =
    EMPATHY_CONTACT_BLOCKING_DIALOG (weak_object);
  gchar *id = user_data;

  if (error != NULL)
    goto error;

  error = g_hash_table_lookup (failed_id_errors, id);
  if (error != NULL)
    goto error;

  tp_contact_block_async (contacts[0], FALSE, block_cb, self);
  goto finally;

error:
  DEBUG ("Error getting contact on %s: %s",
      get_pretty_conn_name (conn), error->message);

  contact_blocking_dialog_set_error (
      EMPATHY_CONTACT_BLOCKING_DIALOG (self), error);

finally:
  g_free (id);
}

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

  tp_connection_get_contacts_by_id (conn, 1, identifiers,
      0, NULL, block_contact_got_contact,
      g_strdup (identifiers[0]), NULL, G_OBJECT (self));

  gtk_entry_set_text (GTK_ENTRY (self->priv->add_contact_entry), "");
  gtk_widget_hide (self->priv->info_bar);
}

static void
unblock_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyContactBlockingDialog *self = user_data;
  GError *error = NULL;

  if (!tp_connection_unblock_contacts_finish (TP_CONNECTION (source), result,
        &error))
    {
      DEBUG ("Error unblocking contacts: %s", error->message);

      contact_blocking_dialog_set_error (
          EMPATHY_CONTACT_BLOCKING_DIALOG (self), error);

      g_error_free (error);
      return;
    }

  DEBUG ("Contacts unblocked");
}

static void
contact_blocking_dialog_remove_contacts (GtkWidget *button,
    EmpathyContactBlockingDialog *self)
{
  TpConnection *conn = empathy_account_chooser_get_connection (
      EMPATHY_ACCOUNT_CHOOSER (self->priv->account_chooser));
  GtkTreeModel *model;
  GList *rows, *ptr;
  GPtrArray *contacts;

  rows = gtk_tree_selection_get_selected_rows (self->priv->selection, &model);

  contacts = g_ptr_array_new_with_free_func (g_object_unref);

  for (ptr = rows; ptr != NULL; ptr = ptr->next)
    {
      GtkTreePath *path = ptr->data;
      GtkTreeIter iter;
      TpContact *contact;

      if (!gtk_tree_model_get_iter (model, &iter, path))
        continue;

      gtk_tree_model_get (model, &iter,
          COL_BLOCKED_CONTACT, &contact,
          -1);

      g_ptr_array_add (contacts, contact);

      gtk_tree_path_free (path);
    }

  g_list_free (rows);

  if (contacts->len > 0)
    {
      DEBUG ("Unblocking %u contacts", contacts->len);

      tp_connection_unblock_contacts_async (conn, contacts->len,
          (TpContact * const *) contacts->pdata, unblock_cb, self);
    }

  g_ptr_array_unref (contacts);
}

static void
contact_blocking_dialog_account_changed (GtkWidget *account_chooser,
    EmpathyContactBlockingDialog *self)
{
  TpConnection *conn = empathy_account_chooser_get_connection (
      EMPATHY_ACCOUNT_CHOOSER (account_chooser));
  GPtrArray *blocked;
  EmpathyContactManager *contact_manager;
  EmpathyTpContactList *contact_list;
  GList *members, *ptr;

  if (self->priv->block_account_changed > 0)
    return;

  if (conn == self->priv->current_conn)
    return;

  /* clear the lists of contacts */
  gtk_list_store_clear (self->priv->blocked_contacts);
  gtk_list_store_clear (self->priv->completion_contacts);

  if (self->priv->current_conn != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->priv->current_conn,
          blocked_contacts_changed_cb, self);

      g_clear_object (&self->priv->current_conn);
    }

  if (conn == NULL)
    return;

  DEBUG ("Account changed: %s", get_pretty_conn_name (conn));

  self->priv->current_conn = g_object_ref (conn);

  tp_g_signal_connect_object (conn, "blocked-contacts-changed",
      G_CALLBACK (blocked_contacts_changed_cb), self, 0);

  blocked = tp_connection_get_blocked_contacts (conn);

  DEBUG ("%u contacts blocked on %s",
      blocked != NULL ? blocked->len : 0, get_pretty_conn_name (conn));

  contact_blocking_dialog_add_blocked (self, blocked);

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

  g_clear_object (&priv->current_conn);

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
  TpSimpleClientFactory *factory;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_CONTACT_BLOCKING_DIALOG,
      EmpathyContactBlockingDialogPrivate);

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
      G_TYPE_STRING, /* text */
      TP_TYPE_CONTACT); /* contact */

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

  factory = tp_proxy_get_factory (am);
  tp_simple_client_factory_add_connection_features_varargs (factory,
      TP_CONNECTION_FEATURE_CONTACT_BLOCKING, NULL);

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
