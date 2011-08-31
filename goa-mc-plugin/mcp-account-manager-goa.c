/*
 * mcp-account-manager-goa.c
 *
 * McpAccountManagerGoa - a Mission Control plugin to expose GNOME Online
 * Accounts with chat capabilities (e.g. Facebook) to Mission Control
 *
 * Copyright (C) 2010-2011 Collabora Ltd.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *    Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#include <glib/gi18n.h>

#include <telepathy-glib/util.h>

#define GOA_API_IS_SUBJECT_TO_CHANGE /* awesome! */
#include <goa/goa.h>

#include "mcp-account-manager-goa.h"

#define DEBUG g_debug
#define GET_PRIVATE(self) (((McpAccountManagerGoa *) self)->priv)
#define DECLARE_GASYNC_CALLBACK(name) \
  static void name (GObject *, GAsyncResult *, gpointer);

#define PLUGIN_NAME "goa"
#define PLUGIN_PRIORITY (MCP_ACCOUNT_STORAGE_PLUGIN_PRIO_KEYRING + 10)
#define PLUGIN_DESCRIPTION "Provide Telepathy Accounts from GOA"
#define PLUGIN_PROVIDER "org.gnome.OnlineAccounts"

static void account_storage_iface_init (McpAccountStorageIface *iface);

G_DEFINE_TYPE_WITH_CODE (McpAccountManagerGoa,
    mcp_account_manager_goa,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (MCP_TYPE_ACCOUNT_STORAGE,
      account_storage_iface_init))

struct _McpAccountManagerGoaPrivate
{
  gboolean ready;

  GoaClient *client;
  GHashTable *accounts; /* alloc'ed string -> ref'ed GoaObject */
};


static void
mcp_account_manager_goa_dispose (GObject *self)
{
  McpAccountManagerGoaPrivate *priv = GET_PRIVATE (self);

  tp_clear_object (&priv->client);

  G_OBJECT_CLASS (mcp_account_manager_goa_parent_class)->dispose (self);
}


static void
mcp_account_manager_goa_finalize (GObject *self)
{
  McpAccountManagerGoaPrivate *priv = GET_PRIVATE (self);

  g_hash_table_destroy (priv->accounts);

  G_OBJECT_CLASS (mcp_account_manager_goa_parent_class)->finalize (self);
}


static void
mcp_account_manager_goa_class_init (McpAccountManagerGoaClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = mcp_account_manager_goa_dispose;
  gobject_class->finalize = mcp_account_manager_goa_finalize;

  g_type_class_add_private (gobject_class,
      sizeof (McpAccountManagerGoaPrivate));
}


// static void
// _enable_chat_toggled (GConfClient *client,
//     guint cnxn_id,
//     GConfEntry *entry,
//     gpointer user_data)
// {
//   McpAccountStorage *self = MCP_ACCOUNT_STORAGE (user_data);
//   McpAccountManagerGoaPrivate *priv = GET_PRIVATE (self);
//   gboolean enabled = gconf_value_get_bool (gconf_entry_get_value (entry));
// 
//   DEBUG ("%s: %s", G_STRFUNC, enabled ? "enabled" : "disabled");
// 
//   if (priv->ready && priv->facebook_available)
//     {
//       DEBUG ("Emitting toggled signal");
// 
//       g_signal_emit_by_name (self, "toggled", FACEBOOK_ACCOUNT_NAME, enabled);
//     }
// }


static GHashTable *
get_tp_parameters (GoaAccount *account)
{
  GHashTable *params = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, g_free);
  const char *type = goa_account_get_provider_type (account);

#define PARAM(key, value) g_hash_table_insert (params, key, g_strdup (value));

 // { "param-account", "chat.facebook.com" },
 // { "param-server", "chat.facebook.com" },

  if (!tp_strdiff (type, "google"))
    {
      PARAM ("manager", "gabble");
      PARAM ("protocol", "jabber");
      PARAM ("Icon", "im-google-talk");
      PARAM ("Service", "google-talk");

      PARAM ("param-account", goa_account_get_identity (account));
      PARAM ("param-server", "talk.google.com");
      PARAM ("param-fallback-servers",
          "talkx.l.google.com;"
          "talkx.l.google.com:443,oldssl;"
          "talkx.l.google.com:80");
      PARAM ("param-extra-certificate-identities", "talk.google.com");
      PARAM ("param-require-encryption", "true");
    }
  else
    {
      /* unknown account type */
      g_hash_table_destroy (params);
      return NULL;
    }

  /* generic properties */
  PARAM ("DisplayName", goa_account_get_presentation_identity (account));
  PARAM ("ConnectAutomatically", "true");

#undef PARAM

  return params;
}


static char *
get_tp_account_name (GoaAccount *account)
{
  GHashTable *params = get_tp_parameters (account);
  const char *type = goa_account_get_provider_type (account);
  const char *id = goa_account_get_id (account);
  char *name;

  if (params == NULL)
    return NULL;

  name = g_strdup_printf ("%s/%s/goa_%s_%s",
      (char *) g_hash_table_lookup (params, "manager"),
      (char *) g_hash_table_lookup (params, "protocol"),
      type, id);

  g_hash_table_destroy (params);

  return name;
}


static void
_new_account (McpAccountManagerGoa *self,
    GoaObject *object)
{
  GoaAccount *account = goa_object_peek_account (object);
  char *account_name = get_tp_account_name (account);

  if (account_name == NULL)
    return;

  /* @account_name now is owned by the hash table */
  g_hash_table_insert (self->priv->accounts, account_name,
      g_object_ref (object));

  if (self->priv->ready)
    g_signal_emit_by_name (self, "created", account_name);
}


DECLARE_GASYNC_CALLBACK (_goa_client_new_cb);

static void
mcp_account_manager_goa_init (McpAccountManagerGoa *self)
{
  DEBUG ("GOA MC plugin initialised");

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      MCP_TYPE_ACCOUNT_MANAGER_GOA, McpAccountManagerGoaPrivate);

  self->priv->accounts = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);

  goa_client_new (NULL, _goa_client_new_cb, self);
}


static void
_account_added_cb (GoaClient *client,
    GoaObject *object,
    McpAccountManagerGoa *self)
{
  _new_account (self, object);
}


static void
_account_removed_cb (GoaClient *client,
    GoaObject *object,
    McpAccountManagerGoa *self)
{
  GoaAccount *account = goa_object_peek_account (object);
  char *name = get_tp_account_name (account);

  if (self->priv->ready)
    g_signal_emit_by_name (self, "deleted", name);

  g_hash_table_remove (self->priv->accounts, name);

  g_free (name);
}


static void
_goa_client_new_cb (GObject *obj,
    GAsyncResult *result,
    gpointer user_data)
{
  McpAccountManagerGoa *self = user_data;
  GoaClient *client;
  GList *accounts, *ptr;
  GError *error = NULL;

  self->priv->client = goa_client_new_finish (result, &error);

  if (error != NULL)
    {
      DEBUG ("Failed to connect to GOA");
      return;
    }

  accounts = goa_client_get_accounts (self->priv->client);

  for (ptr = accounts; ptr != NULL; ptr = ptr->next)
    {
      _new_account (self, ptr->data);
    }

  g_list_free_full (accounts, g_object_unref);

  g_signal_connect (self->priv->client, "account-added",
      G_CALLBACK (_account_added_cb), self);
  g_signal_connect (self->priv->client, "account-removed",
      G_CALLBACK (_account_removed_cb), self);
  /* FIXME: do we need to support account-changed ? */
}


static GList *
mcp_account_manager_goa_list (const McpAccountStorage *self,
    const McpAccountManager *am)
{
  McpAccountManagerGoaPrivate *priv = GET_PRIVATE (self);
  GList *accounts = NULL;
  GHashTableIter iter;
  gpointer key;

  DEBUG (G_STRFUNC);

  g_hash_table_iter_init (&iter, priv->accounts);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    accounts = g_list_prepend (accounts, g_strdup (key));

  return accounts;
}


static void
get_enabled (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *acct)
{
  // McpAccountManagerGoaPrivate *priv = GET_PRIVATE (self);
  // GError *error = NULL;
  // gboolean enabled;
  // const char *value;

  // enabled = gconf_client_get_bool (priv->gconf,
  //     BISHO_FB_GCONF_ENABLE_CHAT_KEY, &error);
  // if (error != NULL)
  //   {
  //     g_warning ("Unabled to get value for %s/Enabled from GConf: %s",
  //         acct, error->message);
  //     g_clear_error (&error);
  //   }

  // value = enabled ? "true" : "false";

  // DEBUG ("Facebook Chat enabled = %s", value);

  mcp_account_manager_set_value (am, acct, "Enabled", "true");
}


static gboolean
mcp_account_manager_goa_get (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *acct,
    const gchar *key)
{
  McpAccountManagerGoaPrivate *priv = GET_PRIVATE (self);
  GoaObject *object;
  GoaAccount *account;

  DEBUG ("%s: %s, %s", G_STRFUNC, acct, key);

  object = g_hash_table_lookup (priv->accounts, acct);

  if (object == NULL)
    return FALSE;

  account = goa_object_peek_account (object);

  if (account == NULL)
    return FALSE;

  if (key == NULL)
    {
      /* load all keys */
      GHashTable *params = get_tp_parameters (account);
      GHashTableIter iter;
      gpointer key, value;

      g_hash_table_iter_init (&iter, params);
      while (g_hash_table_iter_next (&iter, &key, &value))
        mcp_account_manager_set_value (am, acct, key, value);

      g_hash_table_destroy (params);

      get_enabled (self, am, acct);
    }
  else if (!tp_strdiff (key, "Enabled"))
    {
      get_enabled (self, am, acct);
    }
  else
    {
      /* get a specific key */
      GHashTable *params = get_tp_parameters (account);

      mcp_account_manager_set_value (am, acct, key,
          g_hash_table_lookup (params, key));

      g_hash_table_destroy (params);
    }

  return TRUE;
}


static gboolean
mcp_account_manager_goa_set (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *acct,
    const gchar *key,
    const gchar *val)
{
  GError *error = NULL;

  DEBUG ("%s: (%s, %s, %s)", G_STRFUNC, acct, key, val);

  // if (!tp_strdiff (acct, FACEBOOK_ACCOUNT_NAME))
  //   {
  //     if (!tp_strdiff (key, "Enabled"))
  //       {
  //         gboolean enabled = !tp_strdiff (val, "true");

  //         DEBUG ("set Enabled to %s", enabled ? "enabled" : "disabled");

  //         gconf_client_set_bool (priv->gconf, BISHO_FB_GCONF_ENABLE_CHAT_KEY,
  //             enabled, &error);
  //         if (error != NULL)
  //           {
  //             g_warning ("Unable to save %s/Enable state in GConf: %s",
  //                 acct, error->message);
  //             g_clear_error (&error);
  //           }

  //         return TRUE;
  //       }
  //     else
  //       /* pretend we saved everything else */
  //       return TRUE;
  //   }
  // else
  //   {
  //     return FALSE;
  //   }
}


static gboolean
mcp_account_manager_goa_delete (const McpAccountStorage *self,
    const McpAccountManager *am,
    const gchar *acct,
    const gchar *key)
{
  DEBUG ("%s: (%s, %s)", G_STRFUNC, acct, key);

//  if (!tp_strdiff (acct, FACEBOOK_ACCOUNT_NAME))
//    {
//      if (!tp_strdiff (key, "Enabled"))
//        return TRUE;
//      else
//        /* pretend we deleted everything else */
//        return TRUE;
//    }
//  else
//    {
//      return FALSE;
//    }
}


static gboolean
mcp_account_manager_goa_commit (const McpAccountStorage *self,
    const McpAccountManager *am)
{
  DEBUG ("%s", G_STRFUNC);

  return TRUE;
}


static void
mcp_account_manager_goa_ready (const McpAccountStorage *self,
    const McpAccountManager *am)
{
  McpAccountManagerGoaPrivate *priv = GET_PRIVATE (self);

  priv->ready = TRUE;
}


static guint
mcp_account_manager_goa_get_restrictions (const McpAccountStorage *self,
    const gchar *account)
{
  return TP_STORAGE_RESTRICTION_FLAG_CANNOT_SET_PARAMETERS |
         TP_STORAGE_RESTRICTION_FLAG_CANNOT_SET_SERVICE;
}


static void
mcp_account_manager_goa_get_identifier (const McpAccountStorage *self,
    const gchar *acct,
    GValue *identifier)
{
  McpAccountManagerGoaPrivate *priv = GET_PRIVATE (self);
  GoaObject *object;
  GoaAccount *account;

  object = g_hash_table_lookup (priv->accounts, acct);
  g_return_if_fail (object != NULL);

  account = goa_object_peek_account (object);
  g_return_if_fail (account != NULL);

  g_value_init (identifier, G_TYPE_STRING);
  g_value_set_string (identifier, goa_account_get_id (account));
}


static void
account_storage_iface_init (McpAccountStorageIface *iface)
{
  mcp_account_storage_iface_set_name (iface, PLUGIN_NAME);
  mcp_account_storage_iface_set_desc (iface, PLUGIN_DESCRIPTION);
  mcp_account_storage_iface_set_priority (iface, PLUGIN_PRIORITY);
  mcp_account_storage_iface_set_provider (iface, PLUGIN_PROVIDER);

#define IMPLEMENT(x) mcp_account_storage_iface_implement_##x(iface, \
    mcp_account_manager_goa_##x)
  IMPLEMENT (get);
  IMPLEMENT (list);
  IMPLEMENT (set);
  IMPLEMENT (delete);
  IMPLEMENT (commit);
  IMPLEMENT (ready);
  IMPLEMENT (get_restrictions);
  IMPLEMENT (get_identifier);
#undef IMPLEMENT
}
