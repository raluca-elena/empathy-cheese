/*
 * Copyright (C) 2003-2007 Imendio AB
 * Copyright (C) 2007-2011 Collabora Ltd.
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
 * Authors: Richard Hult <richard@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 *
 * Some snippets are taken from GnuTLS 2.8.6, which is distributed under the
 * same GNU Lesser General Public License 2.1 (or later) version. See
 * empathy_get_x509_certified_hostname ().
 */

#include "config.h"

#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>

#include <libxml/uri.h>

#include <folks/folks.h>
#include <folks/folks-telepathy.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/channel.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/util.h>

#include "empathy-client-factory.h"
#include "empathy-utils.h"
#include "empathy-contact-manager.h"
#include "empathy-individual-manager.h"
#include "empathy-presence-manager.h"
#include "empathy-request-util.h"
#include "empathy-tp-contact-factory.h"

#include <extensions/extensions.h>

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include "empathy-debug.h"

/* Translation between presence types and string */
static struct {
  const gchar *name;
  TpConnectionPresenceType type;
} presence_types[] = {
  { "available", TP_CONNECTION_PRESENCE_TYPE_AVAILABLE },
  { "busy",      TP_CONNECTION_PRESENCE_TYPE_BUSY },
  { "away",      TP_CONNECTION_PRESENCE_TYPE_AWAY },
  { "ext_away",  TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY },
  { "hidden",    TP_CONNECTION_PRESENCE_TYPE_HIDDEN },
  { "offline",   TP_CONNECTION_PRESENCE_TYPE_OFFLINE },
  { "unset",     TP_CONNECTION_PRESENCE_TYPE_UNSET },
  { "unknown",   TP_CONNECTION_PRESENCE_TYPE_UNKNOWN },
  { "error",     TP_CONNECTION_PRESENCE_TYPE_ERROR },
  /* alternative names */
  { "dnd",      TP_CONNECTION_PRESENCE_TYPE_BUSY },
  { "brb",      TP_CONNECTION_PRESENCE_TYPE_AWAY },
  { "xa",       TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY },
  { NULL, },
};

static gboolean
properties_contains (gchar **list,
		     gint length,
		     const gchar *property);

static gboolean
check_writeable_property (TpConnection *connection,
			  FolksIndividual *individual,
			  gchar *property);

void
empathy_init (void)
{
  static gboolean initialized = FALSE;
  TpAccountManager *am;
  EmpathyClientFactory *factory;

  if (initialized)
    return;

  g_type_init ();

  /* Setup gettext */
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

  /* Setup debug output for empathy and telepathy-glib */
  if (g_getenv ("EMPATHY_TIMING") != NULL)
    g_log_set_default_handler (tp_debug_timestamped_log_handler, NULL);

  empathy_debug_set_flags (g_getenv ("EMPATHY_DEBUG"));
  tp_debug_divert_messages (g_getenv ("EMPATHY_LOGFILE"));

  emp_cli_init ();

  initialized = TRUE;

  factory = empathy_client_factory_dup ();
  am = tp_account_manager_new_with_factory (TP_SIMPLE_CLIENT_FACTORY (factory));
  tp_account_manager_set_default (am);

  g_object_unref (factory);
  g_object_unref (am);
}

gboolean
empathy_xml_validate (xmlDoc      *doc,
    const gchar *dtd_filename)
{
  gchar *path;
  xmlChar *escaped;
  xmlValidCtxt  cvp;
  xmlDtd *dtd;
  gboolean ret;

  path = g_build_filename (g_getenv ("EMPATHY_SRCDIR"), "libempathy",
         dtd_filename, NULL);
  if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
      g_free (path);
      path = g_build_filename (DATADIR, "empathy", dtd_filename, NULL);
    }

  DEBUG ("Loading dtd file %s", path);

  /* The list of valid chars is taken from libxml. */
  escaped = xmlURIEscapeStr ((const xmlChar *) path,
    (const xmlChar *)":@&=+$,/?;");
  g_free (path);

  memset (&cvp, 0, sizeof (cvp));
  dtd = xmlParseDTD (NULL, escaped);
  ret = xmlValidateDtd (&cvp, doc, dtd);

  xmlFree (escaped);
  xmlFreeDtd (dtd);

  return ret;
}

xmlNodePtr
empathy_xml_node_get_child (xmlNodePtr   node,
    const gchar *child_name)
{
  xmlNodePtr l;

  g_return_val_if_fail (node != NULL, NULL);
  g_return_val_if_fail (child_name != NULL, NULL);

  for (l = node->children; l; l = l->next)
    {
      if (l->name && strcmp ((const gchar *) l->name, child_name) == 0)
        return l;
    }

  return NULL;
}

xmlChar *
empathy_xml_node_get_child_content (xmlNodePtr   node,
    const gchar *child_name)
{
  xmlNodePtr l;

  g_return_val_if_fail (node != NULL, NULL);
  g_return_val_if_fail (child_name != NULL, NULL);

  l = empathy_xml_node_get_child (node, child_name);
  if (l != NULL)
    return xmlNodeGetContent (l);

  return NULL;
}

xmlNodePtr
empathy_xml_node_find_child_prop_value (xmlNodePtr   node,
    const gchar *prop_name,
    const gchar *prop_value)
{
  xmlNodePtr l;
  xmlNodePtr found = NULL;

  g_return_val_if_fail (node != NULL, NULL);
  g_return_val_if_fail (prop_name != NULL, NULL);
  g_return_val_if_fail (prop_value != NULL, NULL);

  for (l = node->children; l && !found; l = l->next)
    {
      xmlChar *prop;

      if (!xmlHasProp (l, (const xmlChar *) prop_name))
        continue;

      prop = xmlGetProp (l, (const xmlChar *) prop_name);
      if (prop && strcmp ((const gchar *) prop, prop_value) == 0)
        found = l;

      xmlFree (prop);
    }

  return found;
}

const gchar *
empathy_presence_get_default_message (TpConnectionPresenceType presence)
{
  switch (presence)
    {
      case TP_CONNECTION_PRESENCE_TYPE_AVAILABLE:
        return _("Available");
      case TP_CONNECTION_PRESENCE_TYPE_BUSY:
        return _("Busy");
      case TP_CONNECTION_PRESENCE_TYPE_AWAY:
      case TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY:
        return _("Away");
      case TP_CONNECTION_PRESENCE_TYPE_HIDDEN:
        return _("Invisible");
      case TP_CONNECTION_PRESENCE_TYPE_OFFLINE:
        return _("Offline");
      case TP_CONNECTION_PRESENCE_TYPE_UNKNOWN:
        /* translators: presence type is unknown */
        return C_("presence", "Unknown");
      case TP_CONNECTION_PRESENCE_TYPE_UNSET:
      case TP_CONNECTION_PRESENCE_TYPE_ERROR:
      default:
        return NULL;
    }

  return NULL;
}

const gchar *
empathy_presence_to_str (TpConnectionPresenceType presence)
{
  int i;

  for (i = 0 ; presence_types[i].name != NULL; i++)
    if (presence == presence_types[i].type)
      return presence_types[i].name;

  return NULL;
}

TpConnectionPresenceType
empathy_presence_from_str (const gchar *str)
{
  int i;

  for (i = 0 ; presence_types[i].name != NULL; i++)
    if (!tp_strdiff (str, presence_types[i].name))
      return presence_types[i].type;

  return TP_CONNECTION_PRESENCE_TYPE_UNSET;
}

static const gchar *
empathy_status_reason_get_default_message (TpConnectionStatusReason reason)
{
  switch (reason)
    {
      case TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED:
        return _("No reason specified");
      case TP_CONNECTION_STATUS_REASON_REQUESTED:
        return _("Status is set to offline");
      case TP_CONNECTION_STATUS_REASON_NETWORK_ERROR:
        return _("Network error");
      case TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED:
        return _("Authentication failed");
      case TP_CONNECTION_STATUS_REASON_ENCRYPTION_ERROR:
        return _("Encryption error");
      case TP_CONNECTION_STATUS_REASON_NAME_IN_USE:
        return _("Name in use");
      case TP_CONNECTION_STATUS_REASON_CERT_NOT_PROVIDED:
        return _("Certificate not provided");
      case TP_CONNECTION_STATUS_REASON_CERT_UNTRUSTED:
        return _("Certificate untrusted");
      case TP_CONNECTION_STATUS_REASON_CERT_EXPIRED:
        return _("Certificate expired");
      case TP_CONNECTION_STATUS_REASON_CERT_NOT_ACTIVATED:
        return _("Certificate not activated");
      case TP_CONNECTION_STATUS_REASON_CERT_HOSTNAME_MISMATCH:
        return _("Certificate hostname mismatch");
      case TP_CONNECTION_STATUS_REASON_CERT_FINGERPRINT_MISMATCH:
        return _("Certificate fingerprint mismatch");
      case TP_CONNECTION_STATUS_REASON_CERT_SELF_SIGNED:
        return _("Certificate self-signed");
      case TP_CONNECTION_STATUS_REASON_CERT_OTHER_ERROR:
        return _("Certificate error");
      default:
        return _("Unknown reason");
    }
}

static GHashTable *
create_errors_to_message_hash (void)
{
  GHashTable *errors;

  errors = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (errors, TP_ERROR_STR_NETWORK_ERROR, _("Network error"));
  g_hash_table_insert (errors, TP_ERROR_STR_AUTHENTICATION_FAILED,
    _("Authentication failed"));
  g_hash_table_insert (errors, TP_ERROR_STR_ENCRYPTION_ERROR,
    _("Encryption error"));
  g_hash_table_insert (errors, TP_ERROR_STR_CERT_NOT_PROVIDED,
    _("Certificate not provided"));
  g_hash_table_insert (errors, TP_ERROR_STR_CERT_UNTRUSTED,
    _("Certificate untrusted"));
  g_hash_table_insert (errors, TP_ERROR_STR_CERT_EXPIRED,
    _("Certificate expired"));
  g_hash_table_insert (errors, TP_ERROR_STR_CERT_NOT_ACTIVATED,
    _("Certificate not activated"));
  g_hash_table_insert (errors, TP_ERROR_STR_CERT_HOSTNAME_MISMATCH,
    _("Certificate hostname mismatch"));
  g_hash_table_insert (errors, TP_ERROR_STR_CERT_FINGERPRINT_MISMATCH,
    _("Certificate fingerprint mismatch"));
  g_hash_table_insert (errors, TP_ERROR_STR_CERT_SELF_SIGNED,
    _("Certificate self-signed"));
  g_hash_table_insert (errors, TP_ERROR_STR_CANCELLED,
    _("Status is set to offline"));
  g_hash_table_insert (errors, TP_ERROR_STR_ENCRYPTION_NOT_AVAILABLE,
    _("Encryption is not available"));
  g_hash_table_insert (errors, TP_ERROR_STR_CERT_INVALID,
    _("Certificate is invalid"));
  g_hash_table_insert (errors, TP_ERROR_STR_CONNECTION_REFUSED,
    _("Connection has been refused"));
  g_hash_table_insert (errors, TP_ERROR_STR_CONNECTION_FAILED,
    _("Connection can't be established"));
  g_hash_table_insert (errors, TP_ERROR_STR_CONNECTION_LOST,
    _("Connection has been lost"));
  g_hash_table_insert (errors, TP_ERROR_STR_ALREADY_CONNECTED,
    _("This resource is already connected to the server"));
  g_hash_table_insert (errors, TP_ERROR_STR_CONNECTION_REPLACED,
    _("Connection has been replaced by a new connection using the "
    "same resource"));
  g_hash_table_insert (errors, TP_ERROR_STR_REGISTRATION_EXISTS,
    _("The account already exists on the server"));
  g_hash_table_insert (errors, TP_ERROR_STR_SERVICE_BUSY,
    _("Server is currently too busy to handle the connection"));
  g_hash_table_insert (errors, TP_ERROR_STR_CERT_REVOKED,
    _("Certificate has been revoked"));
  g_hash_table_insert (errors, TP_ERROR_STR_CERT_INSECURE,
    _("Certificate uses an insecure cipher algorithm or is "
    "cryptographically weak"));
  g_hash_table_insert (errors, TP_ERROR_STR_CERT_LIMIT_EXCEEDED,
    _("The length of the server certificate, or the depth of the "
    "server certificate chain, exceed the limits imposed by the "
    "cryptography library"));
  g_hash_table_insert (errors, TP_ERROR_STR_SOFTWARE_UPGRADE_REQUIRED,
    _("Your software is too old"));

  return errors;
}

static const gchar *
empathy_dbus_error_name_get_default_message  (const gchar *error)
{
  static GHashTable *errors_to_message = NULL;

  if (error == NULL)
    return NULL;

  if (G_UNLIKELY (errors_to_message == NULL))
    errors_to_message = create_errors_to_message_hash ();

  return g_hash_table_lookup (errors_to_message, error);
}

const gchar *
empathy_account_get_error_message (TpAccount *account,
    gboolean *user_requested)
{
  const gchar *dbus_error;
  const gchar *message;
        const GHashTable *details = NULL;
  TpConnectionStatusReason reason;

  dbus_error = tp_account_get_detailed_error (account, &details);

  if (user_requested != NULL)
    {
      if (tp_asv_get_boolean (details, "user-requested", NULL))
        *user_requested = TRUE;
      else
        *user_requested = FALSE;
    }

  message = empathy_dbus_error_name_get_default_message (dbus_error);
  if (message != NULL)
    return message;

  DEBUG ("Don't understand error '%s'; fallback to the status reason (%u)",
    dbus_error, reason);

  tp_account_get_connection_status (account, &reason);

  return empathy_status_reason_get_default_message (reason);
}

gchar *
empathy_file_lookup (const gchar *filename, const gchar *subdir)
{
  gchar *path;

  if (subdir == NULL)
    subdir = ".";

  path = g_build_filename (g_getenv ("EMPATHY_SRCDIR"), subdir, filename, NULL);
  if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
      g_free (path);
      path = g_build_filename (DATADIR, "empathy", filename, NULL);
    }

  return path;
}

guint
empathy_proxy_hash (gconstpointer key)
{
  TpProxy *proxy = TP_PROXY (key);
  TpProxyClass *proxy_class = TP_PROXY_GET_CLASS (key);

  g_return_val_if_fail (TP_IS_PROXY (proxy), 0);
  g_return_val_if_fail (proxy_class->must_have_unique_name, 0);

  return g_str_hash (proxy->object_path) ^ g_str_hash (proxy->bus_name);
}

gboolean
empathy_proxy_equal (gconstpointer a,
    gconstpointer b)
{
  TpProxy *proxy_a = TP_PROXY (a);
  TpProxy *proxy_b = TP_PROXY (b);
  TpProxyClass *proxy_a_class = TP_PROXY_GET_CLASS (a);
  TpProxyClass *proxy_b_class = TP_PROXY_GET_CLASS (b);

  g_return_val_if_fail (TP_IS_PROXY (proxy_a), FALSE);
  g_return_val_if_fail (TP_IS_PROXY (proxy_b), FALSE);
  g_return_val_if_fail (proxy_a_class->must_have_unique_name, 0);
  g_return_val_if_fail (proxy_b_class->must_have_unique_name, 0);

  return g_str_equal (proxy_a->object_path, proxy_b->object_path) &&
         g_str_equal (proxy_a->bus_name, proxy_b->bus_name);
}

gboolean
empathy_check_available_state (void)
{
  TpConnectionPresenceType presence;
  EmpathyPresenceManager *presence_mgr;

  presence_mgr = empathy_presence_manager_dup_singleton ();
  presence = empathy_presence_manager_get_state (presence_mgr);
  g_object_unref (presence_mgr);

  if (presence != TP_CONNECTION_PRESENCE_TYPE_AVAILABLE &&
    presence != TP_CONNECTION_PRESENCE_TYPE_UNSET)
    return FALSE;

  return TRUE;
}

gint
empathy_uint_compare (gconstpointer a,
    gconstpointer b)
{
  return *(guint *) a - *(guint *) b;
}

gchar *
empathy_protocol_icon_name (const gchar *protocol)
{
  if (!tp_strdiff (protocol, "yahoojp"))
    /* Yahoo Japan uses the same icon as Yahoo */
    protocol = "yahoo";
  else if (!tp_strdiff (protocol, "simple"))
    /* SIMPLE uses the same icon as SIP */
    protocol = "sip";
  else if (!tp_strdiff (protocol, "sms"))
    return g_strdup ("phone");

  return g_strdup_printf ("im-%s", protocol);
}

GType
empathy_type_dbus_ao (void)
{
  static GType t = 0;

  if (G_UNLIKELY (t == 0))
     t = dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH);

  return t;
}

const char *
empathy_protocol_name_to_display_name (const gchar *proto_name)
{
  int i;
  static struct {
    const gchar *proto;
    const gchar *display;
    gboolean translated;
  } names[] = {
    { "jabber", "Jabber", FALSE },
    { "msn", "Windows Live (MSN)", FALSE, },
    { "local-xmpp", N_("People Nearby"), TRUE },
    { "irc", "IRC", FALSE },
    { "icq", "ICQ", FALSE },
    { "aim", "AIM", FALSE },
    { "yahoo", "Yahoo!", FALSE },
    { "yahoojp", N_("Yahoo! Japan"), TRUE },
    { "groupwise", "GroupWise", FALSE },
    { "sip", "SIP", FALSE },
    { NULL, NULL }
  };

  for (i = 0; names[i].proto != NULL; i++)
    {
      if (!tp_strdiff (proto_name, names[i].proto))
        {
          if (names[i].translated)
            return gettext (names[i].display);
          else
            return names[i].display;
        }
    }

  return proto_name;
}

const char *
empathy_service_name_to_display_name (const gchar *service_name)
{
  int i;
  static struct {
    const gchar *service;
    const gchar *display;
    gboolean translated;
  } names[] = {
    { "google-talk", N_("Google Talk"), FALSE },
    { "facebook", N_("Facebook Chat"), TRUE },
    { NULL, NULL }
  };

  for (i = 0; names[i].service != NULL; i++)
    {
      if (!tp_strdiff (service_name, names[i].service))
        {
          if (names[i].translated)
            return gettext (names[i].display);
          else
            return names[i].display;
        }
    }

  return service_name;
}

gboolean
empathy_account_manager_get_accounts_connected (gboolean *connecting)
{
  TpAccountManager *manager;
  GList *accounts, *l;
  gboolean out_connecting = FALSE;
  gboolean out_connected = FALSE;

  manager = tp_account_manager_dup ();

  if (G_UNLIKELY (!tp_account_manager_is_prepared (manager,
          TP_ACCOUNT_MANAGER_FEATURE_CORE)))
    g_critical (G_STRLOC ": %s called before AccountManager ready", G_STRFUNC);

  accounts = tp_account_manager_get_valid_accounts (manager);

  for (l = accounts; l != NULL; l = l->next)
    {
      TpConnectionStatus s = tp_account_get_connection_status (
          TP_ACCOUNT (l->data), NULL);

      if (s == TP_CONNECTION_STATUS_CONNECTING)
        out_connecting = TRUE;
      else if (s == TP_CONNECTION_STATUS_CONNECTED)
        out_connected = TRUE;

      if (out_connecting && out_connected)
        break;
    }

  g_list_free (accounts);
  g_object_unref (manager);

  if (connecting != NULL)
    *connecting = out_connecting;

  return out_connected;
}

/* Change the RequestedPresence of a newly created account to ensure that it
 * is actually connected. */
void
empathy_connect_new_account (TpAccount *account,
    TpAccountManager *account_manager)
{
  TpConnectionPresenceType presence;
  gchar *status, *message;

  /* only force presence if presence was offline, unknown or unset */
  presence = tp_account_get_requested_presence (account, NULL, NULL);
  switch (presence)
    {
      case TP_CONNECTION_PRESENCE_TYPE_OFFLINE:
      case TP_CONNECTION_PRESENCE_TYPE_UNKNOWN:
      case TP_CONNECTION_PRESENCE_TYPE_UNSET:
        presence = tp_account_manager_get_most_available_presence (
            account_manager, &status, &message);

        if (presence == TP_CONNECTION_PRESENCE_TYPE_OFFLINE)
          /* Global presence is offline; we force it so user doesn't have to
           * manually change the presence to connect his new account. */
          presence = TP_CONNECTION_PRESENCE_TYPE_AVAILABLE;

        tp_account_request_presence_async (account, presence,
            status, NULL, NULL, NULL);

        g_free (status);
        g_free (message);
        break;

       case TP_CONNECTION_PRESENCE_TYPE_AVAILABLE:
       case TP_CONNECTION_PRESENCE_TYPE_AWAY:
       case TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY:
       case TP_CONNECTION_PRESENCE_TYPE_HIDDEN:
       case TP_CONNECTION_PRESENCE_TYPE_BUSY:
       case TP_CONNECTION_PRESENCE_TYPE_ERROR:
       default:
        /* do nothing if the presence is not offline */
        break;
    }
}

/* Translate Folks' general presence type to the Tp presence type */
TpConnectionPresenceType
empathy_folks_presence_type_to_tp (FolksPresenceType type)
{
  return (TpConnectionPresenceType) type;
}

/* Returns TRUE if the given Individual contains a TpContact */
gboolean
empathy_folks_individual_contains_contact (FolksIndividual *individual)
{
  GeeSet *personas;
  GeeIterator *iter;
  gboolean retval = FALSE;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), FALSE);

  personas = folks_individual_get_personas (individual);
  iter = gee_iterable_iterator (GEE_ITERABLE (personas));
  while (!retval && gee_iterator_next (iter))
    {
      FolksPersona *persona = gee_iterator_get (iter);
      TpContact *contact = NULL;

      if (empathy_folks_persona_is_interesting (persona))
        contact = tpf_persona_get_contact (TPF_PERSONA (persona));

      g_clear_object (&persona);

      if (contact != NULL)
        retval = TRUE;
    }
  g_clear_object (&iter);

  return retval;
}

/* TODO: this needs to be eliminated (and replaced in some cases with user
 * prompts) when we break the assumption that FolksIndividuals are 1:1 with
 * TpContacts */

/* Retrieve the EmpathyContact corresponding to the first TpContact contained
 * within the given Individual. Note that this is a temporary convenience. See
 * the TODO above. */
EmpathyContact *
empathy_contact_dup_from_folks_individual (FolksIndividual *individual)
{
  GeeSet *personas;
  GeeIterator *iter;
  EmpathyContact *contact = NULL;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);

  personas = folks_individual_get_personas (individual);
  iter = gee_iterable_iterator (GEE_ITERABLE (personas));
  while (gee_iterator_next (iter) && (contact == NULL))
    {
      TpfPersona *persona = gee_iterator_get (iter);

      if (empathy_folks_persona_is_interesting (FOLKS_PERSONA (persona)))
        {
          TpContact *tp_contact;

          tp_contact = tpf_persona_get_contact (persona);
          if (tp_contact != NULL)
            {
              contact = empathy_contact_dup_from_tp_contact (tp_contact);
              empathy_contact_set_persona (contact, FOLKS_PERSONA (persona));
            }
        }
      g_clear_object (&persona);
    }
  g_clear_object (&iter);

  if (contact == NULL)
    {
      DEBUG ("Can't create an EmpathyContact for Individual %s",
          folks_individual_get_id (individual));
    }

  return contact;
}

TpChannelGroupChangeReason
tp_channel_group_change_reason_from_folks_groups_change_reason (
    FolksGroupDetailsChangeReason reason)
{
  return (TpChannelGroupChangeReason) reason;
}

TpfPersonaStore *
empathy_dup_persona_store_for_connection (TpConnection *connection)
{
  FolksBackendStore *backend_store;
  FolksBackend *backend;
  TpfPersonaStore *result = NULL;

  backend_store = folks_backend_store_dup ();
  backend = folks_backend_store_dup_backend_by_name (backend_store,
      "telepathy");
  if (backend != NULL)
    {
      GeeMap *stores_map;
      GeeMapIterator *iter;

      stores_map = folks_backend_get_persona_stores (backend);
      iter = gee_map_map_iterator (stores_map);
      while (gee_map_iterator_next (iter))
        {
          TpfPersonaStore *persona_store = gee_map_iterator_get_value (iter);
          TpAccount *account;
          TpConnection *conn_cur;

          account = tpf_persona_store_get_account (persona_store);
          conn_cur = tp_account_get_connection (account);
          if (conn_cur == connection)
            result = persona_store;
        }
      g_clear_object (&iter);
    }

  g_object_unref (backend);
  g_object_unref (backend_store);

  return result;
}

gboolean
empathy_connection_can_add_personas (TpConnection *connection)
{
  gboolean retval;
  FolksPersonaStore *persona_store;

  g_return_val_if_fail (TP_IS_CONNECTION (connection), FALSE);

  if (tp_connection_get_status (connection, NULL) !=
          TP_CONNECTION_STATUS_CONNECTED)
      return FALSE;

  persona_store = FOLKS_PERSONA_STORE (
      empathy_dup_persona_store_for_connection (connection));

  retval = (folks_persona_store_get_can_add_personas (persona_store) ==
      FOLKS_MAYBE_BOOL_TRUE);

  g_clear_object (&persona_store);

  return retval;
}

gboolean
empathy_connection_can_alias_personas (TpConnection *connection,
				       FolksIndividual *individual)
{
  gboolean retval;

  g_return_val_if_fail (TP_IS_CONNECTION (connection), FALSE);

  if (tp_connection_get_status (connection, NULL) !=
          TP_CONNECTION_STATUS_CONNECTED)
      return FALSE;

  retval = check_writeable_property (connection, individual, "alias");

  return retval;
}

gboolean
empathy_connection_can_group_personas (TpConnection *connection,
				       FolksIndividual *individual)
{
  gboolean retval;

  g_return_val_if_fail (TP_IS_CONNECTION (connection), FALSE);

  if (tp_connection_get_status (connection, NULL) !=
          TP_CONNECTION_STATUS_CONNECTED)
      return FALSE;

  retval = check_writeable_property (connection, individual, "groups");

  return retval;
}

gboolean
empathy_folks_persona_is_interesting (FolksPersona *persona)
{
  /* We're not interested in non-Telepathy personas */
  if (!TPF_IS_PERSONA (persona))
    return FALSE;

  /* We're not interested in user personas which haven't been added to the
   * contact list (see bgo#637151). */
  if (folks_persona_get_is_user (persona) &&
      !tpf_persona_get_is_in_contact_list (TPF_PERSONA (persona)))
    {
      return FALSE;
    }

  return TRUE;
}

gchar *
empathy_get_x509_certificate_hostname (gnutls_x509_crt_t cert)
{
  gchar dns_name[256];
  gsize dns_name_size;
  gint idx;
  gint res = 0;

  /* this snippet is taken from GnuTLS.
   * see gnutls/lib/x509/rfc2818_hostname.c
   */
  for (idx = 0; res >= 0; idx++)
    {
      dns_name_size = sizeof (dns_name);
      res = gnutls_x509_crt_get_subject_alt_name (cert, idx,
          dns_name, &dns_name_size, NULL);

      if (res == GNUTLS_SAN_DNSNAME || res == GNUTLS_SAN_IPADDRESS)
        return g_strndup (dns_name, dns_name_size);
    }

  dns_name_size = sizeof (dns_name);
  res = gnutls_x509_crt_get_dn_by_oid (cert, GNUTLS_OID_X520_COMMON_NAME,
      0, 0, dns_name, &dns_name_size);

  if (res >= 0)
    return g_strndup (dns_name, dns_name_size);

  return NULL;
}

gchar *
empathy_format_currency (gint amount,
    guint scale,
    const gchar *currency)
{
#define MINUS "\342\210\222"
#define EURO "\342\202\254"
#define YEN "\302\245"
#define POUND "\302\243"

  /* localised representations of currency */
  /* FIXME: check these, especially negatives and decimals */
  static const struct {
    const char *currency;
    const char *positive;
    const char *negative;
    const char *decimal;
  } currencies[] = {
    /* sym   positive    negative          decimal */
    { "EUR", EURO "%s",  MINUS EURO "%s",  "." },
    { "USD", "$%s",      MINUS "$%s",      "." },
    { "JPY", YEN "%s"    MINUS YEN "%s",   "." },
    { "GBP", POUND "%s", MINUS POUND "%s", "." },
    { "PLN", "%s zl",    MINUS "%s zl",    "." },
    { "BRL", "R$%s",     MINUS "R$%s",     "." },
    { "SEK", "%s kr",    MINUS "%s kr",    "." },
    { "DKK", "kr %s",    "kr " MINUS "%s", "." },
    { "HKD", "$%s",      MINUS "$%s",      "." },
    { "CHF", "%s Fr.",   MINUS "%s Fr.",   "." },
    { "NOK", "kr %s",    "kr" MINUS "%s",  "," },
    { "CAD", "$%s",      MINUS "$%s",      "." },
    { "TWD", "$%s",      MINUS "$%s",      "." },
    { "AUD", "$%s",      MINUS "$%s",      "." },
  };

  const char *positive = "%s";
  const char *negative = MINUS "%s";
  const char *decimal = ".";
  char *fmt_amount, *money;
  guint i;

  /* get the localised currency format */
  for (i = 0; i < G_N_ELEMENTS (currencies); i++)
    {
      if (!tp_strdiff (currency, currencies[i].currency))
        {
          positive = currencies[i].positive;
          negative = currencies[i].negative;
          decimal = currencies[i].decimal;
          break;
        }
    }

  /* format the amount using the scale */
  if (scale == 0)
    {
      /* no decimal point required */
      fmt_amount = g_strdup_printf ("%d", amount);
    }
  else
    {
      /* don't use floating point arithmatic, it's noisy;
       * we take the absolute values, because we want the minus
       * sign to appear before the $ */
      int divisor = pow (10, scale);
      int dollars = abs (amount / divisor);
      int cents = abs (amount % divisor);

      fmt_amount = g_strdup_printf ("%d%s%0*d",
        dollars, decimal, scale, cents);
    }

  money = g_strdup_printf (amount < 0 ? negative : positive, fmt_amount);
  g_free (fmt_amount);

  return money;
}

gboolean
empathy_account_has_uri_scheme_tel (TpAccount *account)
{
  const gchar * const * uri_schemes;
  guint i;

  uri_schemes = tp_account_get_uri_schemes (account);
  if (uri_schemes == NULL)
    return FALSE;

  for (i = 0; uri_schemes[i] != NULL; i++)
    {
      if (!tp_strdiff (uri_schemes[i], "tel"))
        return TRUE;
    }

  return FALSE;
}

/* Return the TpContact on @conn associated with @individual, if any */
TpContact *
empathy_get_tp_contact_for_individual (FolksIndividual *individual,
    TpConnection *conn)
{
  TpContact *contact = NULL;
  GeeSet *personas;
  GeeIterator *iter;

  personas = folks_individual_get_personas (individual);
  iter = gee_iterable_iterator (GEE_ITERABLE (personas));
  while (contact == NULL && gee_iterator_next (iter))
    {
      TpfPersona *persona = gee_iterator_get (iter);
      TpConnection *contact_conn;
      TpContact *contact_cur = NULL;

      if (TPF_IS_PERSONA (persona))
        {
          contact_cur = tpf_persona_get_contact (persona);
          if (contact_cur != NULL)
            {
              contact_conn = tp_contact_get_connection (contact_cur);

              if (!tp_strdiff (tp_proxy_get_object_path (contact_conn),
                    tp_proxy_get_object_path (conn)))
                contact = contact_cur;
            }
        }

      g_clear_object (&persona);
    }
  g_clear_object (&iter);

  return contact;
}

static gboolean
properties_contains (gchar **list,
		     gint length,
		     const gchar *property)
{
  gint i;

  for (i = 0; i < length; i++)
    {
      if (!tp_strdiff (list[i], property))
	return TRUE;
    }

  return FALSE;
}

static gboolean
check_writeable_property (TpConnection *connection,
			  FolksIndividual *individual,
			  gchar *property)
{
  gchar **properties;
  gint prop_len;
  gboolean retval = FALSE;
  GeeSet *personas;
  GeeIterator *iter;
  FolksPersonaStore *persona_store;

  persona_store = FOLKS_PERSONA_STORE (
      empathy_dup_persona_store_for_connection (connection));

  properties =
    folks_persona_store_get_always_writeable_properties (persona_store,
							 &prop_len);
  retval = properties_contains (properties, prop_len, property);
  if (retval == TRUE)
    goto out;

  /* Lets see if the Individual contains a Persona with the given property */
  personas = folks_individual_get_personas (individual);
  iter = gee_iterable_iterator (GEE_ITERABLE (personas));
  while (!retval && gee_iterator_next (iter))
    {
      FolksPersona *persona = gee_iterator_get (iter);

      properties =
	folks_persona_get_writeable_properties (persona, &prop_len);
      retval = properties_contains (properties, prop_len, property);

      g_clear_object (&persona);

      if (retval == TRUE)
	break;
    }
  g_clear_object (&iter);

out:
  g_clear_object (&persona_store);
  return retval;
}

/* Calculate whether the Individual can do audio or video calls.
 * FIXME: We can remove this once libfolks has grown capabilities support
 * again: bgo#626179. */
void
empathy_individual_can_audio_video_call (FolksIndividual *individual,
    gboolean *can_audio_call,
    gboolean *can_video_call,
    EmpathyContact **out_contact)
{
  GeeSet *personas;
  GeeIterator *iter;
  gboolean can_audio = FALSE, can_video = FALSE;

  personas = folks_individual_get_personas (individual);
  iter = gee_iterable_iterator (GEE_ITERABLE (personas));
  while (gee_iterator_next (iter))
    {
      FolksPersona *persona = gee_iterator_get (iter);
      TpContact *tp_contact;

      if (!empathy_folks_persona_is_interesting (persona))
        goto while_finish;

      tp_contact = tpf_persona_get_contact (TPF_PERSONA (persona));
      if (tp_contact != NULL)
        {
          EmpathyContact *contact;

          contact = empathy_contact_dup_from_tp_contact (tp_contact);
          empathy_contact_set_persona (contact, persona);

          can_audio = can_audio || empathy_contact_get_capabilities (contact) &
              EMPATHY_CAPABILITIES_AUDIO;
          can_video = can_video || empathy_contact_get_capabilities (contact) &
              EMPATHY_CAPABILITIES_VIDEO;

          if (out_contact != NULL)
            *out_contact = g_object_ref (contact);

          g_object_unref (contact);
        }

while_finish:
      g_clear_object (&persona);

      if (can_audio && can_video)
        break;
    }

  g_clear_object (&iter);

  if (can_audio_call != NULL)
    *can_audio_call = can_audio;

  if (can_video_call != NULL)
    *can_video_call = can_video;
}

gboolean
empathy_sasl_channel_supports_mechanism (TpChannel *channel,
    const gchar *mechanism)
{
  GHashTable *props;
  const gchar * const *available_mechanisms;

  props = tp_channel_borrow_immutable_properties (channel);
  available_mechanisms = tp_asv_get_boxed (props,
      TP_PROP_CHANNEL_INTERFACE_SASL_AUTHENTICATION_AVAILABLE_MECHANISMS,
      G_TYPE_STRV);

  return tp_strv_contains (available_mechanisms, mechanism);
}
