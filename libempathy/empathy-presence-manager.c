/*
 * Copyright (C) 2007-2011 Collabora Ltd.
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

#include "empathy-presence-manager.h"

#include <config.h>

#include <string.h>

#include <glib/gi18n-lib.h>
#include <dbus/dbus-glib.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/util.h>

#include "empathy-utils.h"
#include "empathy-connectivity.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include "empathy-debug.h"

/* Number of seconds before entering extended autoaway. */
#define EXT_AWAY_TIME (30*60)

/* Number of seconds to consider an account in the "just connected" state
 * for. */
#define ACCOUNT_IS_JUST_CONNECTED_SECONDS 10

struct _EmpathyPresenceManagerPrivate
{
  DBusGProxy *gs_proxy;
  EmpathyConnectivity *connectivity;
  gulong state_change_signal_id;

  gboolean ready;

  TpConnectionPresenceType state;
  gchar *status;
  gboolean auto_away;

  TpConnectionPresenceType away_saved_state;
  TpConnectionPresenceType saved_state;
  gchar *saved_status;

  gboolean is_idle;
  guint ext_away_timeout;

  TpAccountManager *manager;
  gulong most_available_presence_changed_id;

  /* pointer to a TpAccount --> glong of time of connection */
  GHashTable *connect_times;

  TpConnectionPresenceType requested_presence_type;
  gchar *requested_status_message;
};

typedef enum
{
  SESSION_STATUS_AVAILABLE,
  SESSION_STATUS_INVISIBLE,
  SESSION_STATUS_BUSY,
  SESSION_STATUS_IDLE,
  SESSION_STATUS_UNKNOWN
} SessionStatus;

enum
{
  PROP_0,
  PROP_STATE,
  PROP_STATUS,
  PROP_AUTO_AWAY
};

G_DEFINE_TYPE (EmpathyPresenceManager, empathy_presence_manager, G_TYPE_OBJECT);

static EmpathyPresenceManager * singleton = NULL;

static const gchar *presence_type_to_status[NUM_TP_CONNECTION_PRESENCE_TYPES] =
{
  NULL,
  "offline",
  "available",
  "away",
  "xa",
  "hidden",
  "busy",
  NULL,
  NULL,
};

static void
most_available_presence_changed (TpAccountManager *manager,
    TpConnectionPresenceType state,
    gchar *status,
    gchar *status_message,
    EmpathyPresenceManager *self)
{
  if (state == TP_CONNECTION_PRESENCE_TYPE_UNSET)
    /* Assume our presence is offline if MC reports UNSET */
    state = TP_CONNECTION_PRESENCE_TYPE_OFFLINE;

  DEBUG ("Presence changed to '%s' (%d) \"%s\"", status, state,
    status_message);

  g_free (self->priv->status);
  self->priv->state = state;
  if (EMP_STR_EMPTY (status_message))
    self->priv->status = NULL;
  else
    self->priv->status = g_strdup (status_message);

  g_object_notify (G_OBJECT (self), "state");
  g_object_notify (G_OBJECT (self), "status");
}

static gboolean
ext_away_cb (EmpathyPresenceManager *self)
{
  DEBUG ("Going to extended autoaway");
  empathy_presence_manager_set_state (self,
      TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY);
  self->priv->ext_away_timeout = 0;

  return FALSE;
}

static void
next_away_stop (EmpathyPresenceManager *self)
{
  if (self->priv->ext_away_timeout)
    {
      g_source_remove (self->priv->ext_away_timeout);
      self->priv->ext_away_timeout = 0;
    }
}

static void
ext_away_start (EmpathyPresenceManager *self)
{
  if (self->priv->ext_away_timeout != 0)
    return;

  self->priv->ext_away_timeout = g_timeout_add_seconds (EXT_AWAY_TIME,
      (GSourceFunc) ext_away_cb, self);
}

static void
session_status_changed_cb (DBusGProxy *gs_proxy,
    SessionStatus status,
    EmpathyPresenceManager *self)
{
  gboolean is_idle;

  is_idle = (status == SESSION_STATUS_IDLE);

  DEBUG ("Session idle state changed, %s -> %s",
    self->priv->is_idle ? "yes" : "no",
    is_idle ? "yes" : "no");

  if (!self->priv->auto_away ||
      (self->priv->saved_state == TP_CONNECTION_PRESENCE_TYPE_UNSET &&
       (self->priv->state <= TP_CONNECTION_PRESENCE_TYPE_OFFLINE ||
        self->priv->state == TP_CONNECTION_PRESENCE_TYPE_HIDDEN)))
    {
      /* We don't want to go auto away OR we explicitely asked to be
       * offline, nothing to do here */
      self->priv->is_idle = is_idle;
      return;
    }

  if (is_idle && !self->priv->is_idle)
    {
      TpConnectionPresenceType new_state;
      /* We are now idle */

      ext_away_start (self);

      if (self->priv->saved_state != TP_CONNECTION_PRESENCE_TYPE_UNSET)
        /* We are disconnected, when coming back from away
         * we want to restore the presence before the
         * disconnection. */
        self->priv->away_saved_state = self->priv->saved_state;
      else
        self->priv->away_saved_state = self->priv->state;

    new_state = TP_CONNECTION_PRESENCE_TYPE_AWAY;
    if (self->priv->state == TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY)
      new_state = TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY;

    DEBUG ("Going to autoaway. Saved state=%d, new state=%d",
      self->priv->away_saved_state, new_state);
    empathy_presence_manager_set_state (self, new_state);
  }
  else if (!is_idle && self->priv->is_idle)
    {
      /* We are no more idle, restore state */

      next_away_stop (self);

      /* Only try and set the presence if the away saved state is not
       * unset. This is an odd case because it means that the session
       * didn't notify us of the state change to idle, and as a
       * result, we couldn't save the current state at that time.
       */
      if (self->priv->away_saved_state != TP_CONNECTION_PRESENCE_TYPE_UNSET)
        {
          DEBUG ("Restoring state to %d",
            self->priv->away_saved_state);

          empathy_presence_manager_set_state (self,
              self->priv->away_saved_state);
        }
      else
        {
          DEBUG ("Away saved state is unset. This means that we "
                 "weren't told when the session went idle. "
                 "As a result, I'm not trying to set presence");
        }

      self->priv->away_saved_state = TP_CONNECTION_PRESENCE_TYPE_UNSET;
    }

  self->priv->is_idle = is_idle;
}

static void
state_change_cb (EmpathyConnectivity *connectivity,
    gboolean new_online,
    EmpathyPresenceManager *self)
{
  if (!new_online)
    {
      /* We are no longer connected */
      DEBUG ("Disconnected: Save state %d (%s)",
          self->priv->state, self->priv->status);
      self->priv->saved_state = self->priv->state;
      g_free (self->priv->saved_status);
      self->priv->saved_status = g_strdup (self->priv->status);
      empathy_presence_manager_set_state (self,
          TP_CONNECTION_PRESENCE_TYPE_OFFLINE);
    }
  else if (new_online
      && self->priv->saved_state != TP_CONNECTION_PRESENCE_TYPE_UNSET)
    {
      /* We are now connected */
      DEBUG ("Reconnected: Restore state %d (%s)",
          self->priv->saved_state, self->priv->saved_status);
      empathy_presence_manager_set_presence (self,
          self->priv->saved_state,
          self->priv->saved_status);
      self->priv->saved_state = TP_CONNECTION_PRESENCE_TYPE_UNSET;
      g_free (self->priv->saved_status);
      self->priv->saved_status = NULL;
    }
}

static void
presence_manager_finalize (GObject *object)
{
  EmpathyPresenceManager *self = (EmpathyPresenceManager *) object;

  g_free (self->priv->status);
  g_free (self->priv->requested_status_message);

  if (self->priv->gs_proxy)
    g_object_unref (self->priv->gs_proxy);

  g_signal_handler_disconnect (self->priv->connectivity,
      self->priv->state_change_signal_id);
  self->priv->state_change_signal_id = 0;

  if (self->priv->manager != NULL)
    {
      g_signal_handler_disconnect (self->priv->manager,
        self->priv->most_available_presence_changed_id);
      g_object_unref (self->priv->manager);
    }

  g_object_unref (self->priv->connectivity);

  g_hash_table_destroy (self->priv->connect_times);
  self->priv->connect_times = NULL;

  next_away_stop (EMPATHY_PRESENCE_MANAGER (object));

  G_OBJECT_CLASS (empathy_presence_manager_parent_class)->finalize (object);
}

static GObject *
presence_manager_constructor (GType type,
    guint n_props,
    GObjectConstructParam *props)
{
  GObject *retval;

  if (singleton)
    {
      retval = g_object_ref (singleton);
    }
  else
    {
      retval = G_OBJECT_CLASS (empathy_presence_manager_parent_class)->
        constructor (type, n_props, props);

      singleton = EMPATHY_PRESENCE_MANAGER (retval);
      g_object_add_weak_pointer (retval, (gpointer) &singleton);
  }

  return retval;
}

static const gchar *
empathy_presence_manager_get_status (EmpathyPresenceManager *self)
{
  if (G_UNLIKELY (!self->priv->ready))
    g_critical (G_STRLOC ": %s called before AccountManager ready",
        G_STRFUNC);

  if (!self->priv->status)
    return empathy_presence_get_default_message (self->priv->state);

  return self->priv->status;
}

static void
presence_manager_get_property (GObject *object,
    guint param_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyPresenceManager *self = EMPATHY_PRESENCE_MANAGER (object);

  switch (param_id)
    {
      case PROP_STATE:
        g_value_set_enum (value, empathy_presence_manager_get_state (self));
        break;
      case PROP_STATUS:
        g_value_set_string (value, empathy_presence_manager_get_status (self));
        break;
      case PROP_AUTO_AWAY:
        g_value_set_boolean (value,
            empathy_presence_manager_get_auto_away (self));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    };
}

static void
presence_manager_set_property (GObject *object,
    guint param_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyPresenceManager *self = EMPATHY_PRESENCE_MANAGER (object);

  switch (param_id)
    {
      case PROP_STATE:
        empathy_presence_manager_set_state (self, g_value_get_enum (value));
        break;
      case PROP_STATUS:
        empathy_presence_manager_set_status (self, g_value_get_string (value));
        break;
      case PROP_AUTO_AWAY:
        empathy_presence_manager_set_auto_away (self,
            g_value_get_boolean (value));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    };
}

static void
empathy_presence_manager_class_init (EmpathyPresenceManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = presence_manager_finalize;
  object_class->constructor = presence_manager_constructor;
  object_class->get_property = presence_manager_get_property;
  object_class->set_property = presence_manager_set_property;

  g_object_class_install_property (object_class,
      PROP_STATE,
      g_param_spec_uint ("state", "state", "state",
        0, NUM_TP_CONNECTION_PRESENCE_TYPES,
        TP_CONNECTION_PRESENCE_TYPE_UNSET,
        G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
      PROP_STATUS,
      g_param_spec_string ("status","status", "status",
        NULL,
        G_PARAM_READWRITE));

   g_object_class_install_property (object_class,
       PROP_AUTO_AWAY,
       g_param_spec_boolean ("auto-away", "Automatic set presence to away",
         "Should it set presence to away if inactive",
         FALSE,
         G_PARAM_READWRITE));

  g_type_class_add_private (object_class,
      sizeof (EmpathyPresenceManagerPrivate));
}

static void
account_status_changed_cb (TpAccount  *account,
    guint old_status,
    guint new_status,
    guint reason,
    gchar *dbus_error_name,
    GHashTable *details,
    gpointer user_data)
{
  EmpathyPresenceManager *self = EMPATHY_PRESENCE_MANAGER (user_data);
  GTimeVal val;

  if (new_status == TP_CONNECTION_STATUS_CONNECTED)
    {
      g_get_current_time (&val);
      g_hash_table_insert (self->priv->connect_times, account,
               GINT_TO_POINTER (val.tv_sec));
    }
  else if (new_status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
      g_hash_table_remove (self->priv->connect_times, account);
    }
}

static void
account_manager_ready_cb (GObject *source_object,
        GAsyncResult *result,
        gpointer user_data)
{
  EmpathyPresenceManager *self = user_data;
  TpAccountManager *account_manager = TP_ACCOUNT_MANAGER (source_object);
  TpConnectionPresenceType state;
  gchar *status, *status_message;
  GList *accounts, *l;
  GError *error = NULL;

  /* In case we've been finalized before reading this callback */
  if (singleton == NULL)
    return;

  self->priv->ready = TRUE;

  if (!tp_account_manager_prepare_finish (account_manager, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      return;
    }

  state = tp_account_manager_get_most_available_presence (self->priv->manager,
    &status, &status_message);

  most_available_presence_changed (account_manager, state, status,
    status_message, self);

  accounts = tp_account_manager_get_valid_accounts (self->priv->manager);
  for (l = accounts; l != NULL; l = l->next)
    {
      tp_g_signal_connect_object (l->data, "status-changed",
          G_CALLBACK (account_status_changed_cb), self, 0);
    }
  g_list_free (accounts);

  g_free (status);
  g_free (status_message);
}

static void
empathy_presence_manager_init (EmpathyPresenceManager *self)
{
  TpDBusDaemon *dbus;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_PRESENCE_MANAGER, EmpathyPresenceManagerPrivate);

  self->priv->is_idle = FALSE;

  self->priv->manager = tp_account_manager_dup ();

  tp_account_manager_prepare_async (self->priv->manager, NULL,
      account_manager_ready_cb, self);

  self->priv->most_available_presence_changed_id = g_signal_connect (
      self->priv->manager,
      "most-available-presence-changed",
      G_CALLBACK (most_available_presence_changed), self);

  dbus = tp_dbus_daemon_dup (NULL);

  self->priv->gs_proxy = dbus_g_proxy_new_for_name (
      tp_proxy_get_dbus_connection (dbus),
      "org.gnome.SessionManager",
      "/org/gnome/SessionManager/Presence",
      "org.gnome.SessionManager.Presence");

  if (self->priv->gs_proxy)
    {
      dbus_g_proxy_add_signal (self->priv->gs_proxy, "StatusChanged",
          G_TYPE_UINT, G_TYPE_INVALID);
      dbus_g_proxy_connect_signal (self->priv->gs_proxy, "StatusChanged",
          G_CALLBACK (session_status_changed_cb),
          self, NULL);
    }
  else
    {
      DEBUG ("Failed to get gs proxy");
    }

  g_object_unref (dbus);

  self->priv->connectivity = empathy_connectivity_dup_singleton ();
  self->priv->state_change_signal_id = g_signal_connect (
      self->priv->connectivity,
      "state-change", G_CALLBACK (state_change_cb), self);

  self->priv->connect_times = g_hash_table_new (g_direct_hash, g_direct_equal);
}

EmpathyPresenceManager *
empathy_presence_manager_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_PRESENCE_MANAGER, NULL);
}

TpConnectionPresenceType
empathy_presence_manager_get_state (EmpathyPresenceManager *self)
{
  if (G_UNLIKELY (!self->priv->ready))
    g_critical (G_STRLOC ": %s called before AccountManager ready",
        G_STRFUNC);

  return self->priv->state;
}

void
empathy_presence_manager_set_state (EmpathyPresenceManager *self,
    TpConnectionPresenceType state)
{
  empathy_presence_manager_set_presence (self, state, self->priv->status);
}

void
empathy_presence_manager_set_status (EmpathyPresenceManager *self,
       const gchar *status)
{
  empathy_presence_manager_set_presence (self, self->priv->state, status);
}

static void
empathy_presence_manager_do_set_presence (EmpathyPresenceManager *self,
    TpConnectionPresenceType status_type,
    const gchar *status_message)
{
  const gchar *status;

  g_assert (status_type > 0 && status_type < NUM_TP_CONNECTION_PRESENCE_TYPES);

  status = presence_type_to_status[status_type];

  g_return_if_fail (status != NULL);

  /* We possibly should be sure that the account manager is prepared, but
   * sometimes this isn't possible, like when exiting. In other words,
   * we need a callback to empathy_presence_manager_set_presence to be sure the
   * presence is set on all accounts successfully.
   * However, in practice, this is fine as we've already prepared the
   * account manager here in _init. */
  tp_account_manager_set_all_requested_presences (self->priv->manager,
    status_type, status, status_message);
}

void
empathy_presence_manager_set_presence (EmpathyPresenceManager *self,
    TpConnectionPresenceType state,
    const gchar *status)
{
  const gchar     *default_status;

  DEBUG ("Changing presence to %s (%d)", status, state);

  g_free (self->priv->requested_status_message);
  self->priv->requested_presence_type = state;
  self->priv->requested_status_message = g_strdup (status);

  /* Do not set translated default messages */
  default_status = empathy_presence_get_default_message (state);
  if (!tp_strdiff (status, default_status))
    status = NULL;

  if (state != TP_CONNECTION_PRESENCE_TYPE_OFFLINE &&
      !empathy_connectivity_is_online (self->priv->connectivity))
    {
      DEBUG ("Empathy is not online");

      self->priv->saved_state = state;
      if (tp_strdiff (self->priv->status, status))
        {
          g_free (self->priv->saved_status);
          self->priv->saved_status = NULL;
          if (!EMP_STR_EMPTY (status))
            self->priv->saved_status = g_strdup (status);
        }
      return;
    }

  empathy_presence_manager_do_set_presence (self, state, status);
}

gboolean
empathy_presence_manager_get_auto_away (EmpathyPresenceManager *self)
{
  return self->priv->auto_away;
}

void
empathy_presence_manager_set_auto_away (EmpathyPresenceManager *self,
          gboolean     auto_away)
{
  self->priv->auto_away = auto_away;

  g_object_notify (G_OBJECT (self), "auto-away");
}

TpConnectionPresenceType
empathy_presence_manager_get_requested_presence (EmpathyPresenceManager *self,
    gchar **status,
    gchar **status_message)
{
  if (status != NULL)
    *status = g_strdup (presence_type_to_status[
        self->priv->requested_presence_type]);

  if (status_message != NULL)
    *status_message = g_strdup (self->priv->requested_status_message);

  return self->priv->requested_presence_type;
}

/* This function returns %TRUE if EmpathyPresenceManager considers the account
 * @account as having just connected recently. Otherwise, it returns
 * %FALSE. In doubt, %FALSE is returned. */
gboolean
empathy_presence_manager_account_is_just_connected (
    EmpathyPresenceManager *self,
    TpAccount *account)
{
  GTimeVal val;
  gpointer ptr;
  glong t;

  if (tp_account_get_connection_status (account, NULL)
      != TP_CONNECTION_STATUS_CONNECTED)
    return FALSE;

  ptr = g_hash_table_lookup (self->priv->connect_times, account);

  if (ptr == NULL)
    return FALSE;

  t = GPOINTER_TO_INT (ptr);

  g_get_current_time (&val);

  return (val.tv_sec - t) < ACCOUNT_IS_JUST_CONNECTED_SECONDS;
}
