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

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyPresenceManager)
typedef struct {
	DBusGProxy     *gs_proxy;
	EmpathyConnectivity *connectivity;
	gulong state_change_signal_id;

	gboolean ready;

	TpConnectionPresenceType      state;
	gchar          *status;
	gboolean        auto_away;

	TpConnectionPresenceType      away_saved_state;
	TpConnectionPresenceType      saved_state;
	gchar          *saved_status;

	gboolean        is_idle;
	guint           ext_away_timeout;

	TpAccountManager *manager;
	gulong idle_presence_changed_id;

	/* pointer to a TpAccount --> glong of time of connection */
	GHashTable *connect_times;

	TpConnectionPresenceType requested_presence_type;
	gchar *requested_status_message;

} EmpathyPresenceManagerPriv;

typedef enum {
	SESSION_STATUS_AVAILABLE,
	SESSION_STATUS_INVISIBLE,
	SESSION_STATUS_BUSY,
	SESSION_STATUS_IDLE,
	SESSION_STATUS_UNKNOWN
} SessionStatus;

enum {
	PROP_0,
	PROP_STATE,
	PROP_STATUS,
	PROP_AUTO_AWAY
};

G_DEFINE_TYPE (EmpathyPresenceManager, empathy_presence_manager, G_TYPE_OBJECT);

static EmpathyPresenceManager * idle_singleton = NULL;

static const gchar *presence_type_to_status[NUM_TP_CONNECTION_PRESENCE_TYPES] = {
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
idle_presence_changed_cb (TpAccountManager *manager,
			  TpConnectionPresenceType state,
			  gchar          *status,
			  gchar          *status_message,
			  EmpathyPresenceManager    *idle)
{
	EmpathyPresenceManagerPriv *priv;

	priv = GET_PRIV (idle);

	if (state == TP_CONNECTION_PRESENCE_TYPE_UNSET)
		/* Assume our presence is offline if MC reports UNSET */
		state = TP_CONNECTION_PRESENCE_TYPE_OFFLINE;

	DEBUG ("Presence changed to '%s' (%d) \"%s\"", status, state,
		status_message);

	g_free (priv->status);
	priv->state = state;
	if (EMP_STR_EMPTY (status_message))
		priv->status = NULL;
	else
		priv->status = g_strdup (status_message);

	g_object_notify (G_OBJECT (idle), "state");
	g_object_notify (G_OBJECT (idle), "status");
}

static gboolean
idle_ext_away_cb (EmpathyPresenceManager *idle)
{
	EmpathyPresenceManagerPriv *priv;

	priv = GET_PRIV (idle);

	DEBUG ("Going to extended autoaway");
	empathy_presence_manager_set_state (idle, TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY);
	priv->ext_away_timeout = 0;

	return FALSE;
}

static void
idle_ext_away_stop (EmpathyPresenceManager *idle)
{
	EmpathyPresenceManagerPriv *priv;

	priv = GET_PRIV (idle);

	if (priv->ext_away_timeout) {
		g_source_remove (priv->ext_away_timeout);
		priv->ext_away_timeout = 0;
	}
}

static void
idle_ext_away_start (EmpathyPresenceManager *idle)
{
	EmpathyPresenceManagerPriv *priv;

	priv = GET_PRIV (idle);

	if (priv->ext_away_timeout != 0) {
		return;
	}
	priv->ext_away_timeout = g_timeout_add_seconds (EXT_AWAY_TIME,
							(GSourceFunc) idle_ext_away_cb,
							idle);
}

static void
idle_session_status_changed_cb (DBusGProxy    *gs_proxy,
				SessionStatus  status,
				EmpathyPresenceManager   *idle)
{
	EmpathyPresenceManagerPriv *priv;
	gboolean is_idle;

	priv = GET_PRIV (idle);

	is_idle = (status == SESSION_STATUS_IDLE);

	DEBUG ("Session idle state changed, %s -> %s",
		priv->is_idle ? "yes" : "no",
		is_idle ? "yes" : "no");

	if (!priv->auto_away ||
	    (priv->saved_state == TP_CONNECTION_PRESENCE_TYPE_UNSET &&
	     (priv->state <= TP_CONNECTION_PRESENCE_TYPE_OFFLINE ||
	      priv->state == TP_CONNECTION_PRESENCE_TYPE_HIDDEN))) {
		/* We don't want to go auto away OR we explicitely asked to be
		 * offline, nothing to do here */
		priv->is_idle = is_idle;
		return;
	}

	if (is_idle && !priv->is_idle) {
		TpConnectionPresenceType new_state;
		/* We are now idle */

		idle_ext_away_start (idle);

		if (priv->saved_state != TP_CONNECTION_PRESENCE_TYPE_UNSET) {
		    	/* We are disconnected, when coming back from away
		    	 * we want to restore the presence before the
		    	 * disconnection. */
			priv->away_saved_state = priv->saved_state;
		} else {
			priv->away_saved_state = priv->state;
		}

		new_state = TP_CONNECTION_PRESENCE_TYPE_AWAY;
		if (priv->state == TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY) {
			new_state = TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY;
		}

		DEBUG ("Going to autoaway. Saved state=%d, new state=%d",
			priv->away_saved_state, new_state);
		empathy_presence_manager_set_state (idle, new_state);
	} else if (!is_idle && priv->is_idle) {
		/* We are no more idle, restore state */

		idle_ext_away_stop (idle);

		/* Only try and set the presence if the away saved state is not
		 * unset. This is an odd case because it means that the session
		 * didn't notify us of the state change to idle, and as a
		 * result, we couldn't save the current state at that time.
		 */
		if (priv->away_saved_state != TP_CONNECTION_PRESENCE_TYPE_UNSET) {
			DEBUG ("Restoring state to %d",
				priv->away_saved_state);

			empathy_presence_manager_set_state (idle,priv->away_saved_state);
		} else {
			DEBUG ("Away saved state is unset. This means that we "
			       "weren't told when the session went idle. "
			       "As a result, I'm not trying to set presence");
		}

		priv->away_saved_state = TP_CONNECTION_PRESENCE_TYPE_UNSET;
	}

	priv->is_idle = is_idle;
}

static void
idle_state_change_cb (EmpathyConnectivity *connectivity,
		      gboolean new_online,
		      EmpathyPresenceManager *idle)
{
	EmpathyPresenceManagerPriv *priv;

	priv = GET_PRIV (idle);

	if (!new_online) {
		/* We are no longer connected */
		DEBUG ("Disconnected: Save state %d (%s)",
				priv->state, priv->status);
		priv->saved_state = priv->state;
		g_free (priv->saved_status);
		priv->saved_status = g_strdup (priv->status);
		empathy_presence_manager_set_state (idle, TP_CONNECTION_PRESENCE_TYPE_OFFLINE);
	}
	else if (new_online
			&& priv->saved_state != TP_CONNECTION_PRESENCE_TYPE_UNSET) {
		/* We are now connected */
		DEBUG ("Reconnected: Restore state %d (%s)",
				priv->saved_state, priv->saved_status);
		empathy_presence_manager_set_presence (idle,
				priv->saved_state,
				priv->saved_status);
		priv->saved_state = TP_CONNECTION_PRESENCE_TYPE_UNSET;
		g_free (priv->saved_status);
		priv->saved_status = NULL;
	}
}

static void
idle_finalize (GObject *object)
{
	EmpathyPresenceManagerPriv *priv;

	priv = GET_PRIV (object);

	g_free (priv->status);
	g_free (priv->requested_status_message);

	if (priv->gs_proxy) {
		g_object_unref (priv->gs_proxy);
	}

	g_signal_handler_disconnect (priv->connectivity,
				     priv->state_change_signal_id);
	priv->state_change_signal_id = 0;

	if (priv->manager != NULL) {
		g_signal_handler_disconnect (priv->manager,
			priv->idle_presence_changed_id);
		g_object_unref (priv->manager);
	}

	g_object_unref (priv->connectivity);

	g_hash_table_destroy (priv->connect_times);
	priv->connect_times = NULL;

	idle_ext_away_stop (EMPATHY_PRESENCE_MANAGER (object));
}

static GObject *
idle_constructor (GType type,
		  guint n_props,
		  GObjectConstructParam *props)
{
	GObject *retval;

	if (idle_singleton) {
		retval = g_object_ref (idle_singleton);
	} else {
		retval = G_OBJECT_CLASS (empathy_presence_manager_parent_class)->constructor
			(type, n_props, props);

		idle_singleton = EMPATHY_PRESENCE_MANAGER (retval);
		g_object_add_weak_pointer (retval, (gpointer) &idle_singleton);
	}

	return retval;
}

static const gchar *
empathy_presence_manager_get_status (EmpathyPresenceManager *idle)
{
	EmpathyPresenceManagerPriv *priv;

	priv = GET_PRIV (idle);

	if (G_UNLIKELY (!priv->ready))
		g_critical (G_STRLOC ": %s called before AccountManager ready",
				G_STRFUNC);

	if (!priv->status) {
		return empathy_presence_get_default_message (priv->state);
	}

	return priv->status;
}

static void
idle_get_property (GObject    *object,
		   guint       param_id,
		   GValue     *value,
		   GParamSpec *pspec)
{
	EmpathyPresenceManagerPriv *priv;
	EmpathyPresenceManager     *idle;

	priv = GET_PRIV (object);
	idle = EMPATHY_PRESENCE_MANAGER (object);

	switch (param_id) {
	case PROP_STATE:
		g_value_set_enum (value, empathy_presence_manager_get_state (idle));
		break;
	case PROP_STATUS:
		g_value_set_string (value, empathy_presence_manager_get_status (idle));
		break;
	case PROP_AUTO_AWAY:
		g_value_set_boolean (value, empathy_presence_manager_get_auto_away (idle));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
idle_set_property (GObject      *object,
		   guint         param_id,
		   const GValue *value,
		   GParamSpec   *pspec)
{
	EmpathyPresenceManagerPriv *priv;
	EmpathyPresenceManager     *idle;

	priv = GET_PRIV (object);
	idle = EMPATHY_PRESENCE_MANAGER (object);

	switch (param_id) {
	case PROP_STATE:
		empathy_presence_manager_set_state (idle, g_value_get_enum (value));
		break;
	case PROP_STATUS:
		empathy_presence_manager_set_status (idle, g_value_get_string (value));
		break;
	case PROP_AUTO_AWAY:
		empathy_presence_manager_set_auto_away (idle, g_value_get_boolean (value));
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

	object_class->finalize = idle_finalize;
	object_class->constructor = idle_constructor;
	object_class->get_property = idle_get_property;
	object_class->set_property = idle_set_property;

	g_object_class_install_property (object_class,
					 PROP_STATE,
					 g_param_spec_uint ("state",
							    "state",
							    "state",
							    0, NUM_TP_CONNECTION_PRESENCE_TYPES,
							    TP_CONNECTION_PRESENCE_TYPE_UNSET,
							    G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_STATUS,
					 g_param_spec_string ("status",
							      "status",
							      "status",
							      NULL,
							      G_PARAM_READWRITE));

	 g_object_class_install_property (object_class,
					  PROP_AUTO_AWAY,
					  g_param_spec_boolean ("auto-away",
								"Automatic set presence to away",
								"Should it set presence to away if inactive",
								FALSE,
								G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof (EmpathyPresenceManagerPriv));
}

static void
account_status_changed_cb (TpAccount  *account,
			   guint       old_status,
			   guint       new_status,
			   guint       reason,
			   gchar      *dbus_error_name,
			   GHashTable *details,
			   gpointer    user_data)
{
	EmpathyPresenceManager *idle = EMPATHY_PRESENCE_MANAGER (user_data);
	EmpathyPresenceManagerPriv *priv = GET_PRIV (idle);
	GTimeVal val;

	if (new_status == TP_CONNECTION_STATUS_CONNECTED) {
		g_get_current_time (&val);
		g_hash_table_insert (priv->connect_times, account,
				     GINT_TO_POINTER (val.tv_sec));
	} else if (new_status == TP_CONNECTION_STATUS_DISCONNECTED) {
		g_hash_table_remove (priv->connect_times, account);
	}
}

static void
account_manager_ready_cb (GObject *source_object,
			  GAsyncResult *result,
			  gpointer user_data)
{
	EmpathyPresenceManager *idle = user_data;
	TpAccountManager *account_manager = TP_ACCOUNT_MANAGER (source_object);
	EmpathyPresenceManagerPriv *priv;
	TpConnectionPresenceType state;
	gchar *status, *status_message;
	GList *accounts, *l;
	GError *error = NULL;

	/* In case we've been finalized before reading this callback */
	if (idle_singleton == NULL)
		return;

	priv = GET_PRIV (idle);
	priv->ready = TRUE;

	if (!tp_account_manager_prepare_finish (account_manager, result, &error)) {
		DEBUG ("Failed to prepare account manager: %s", error->message);
		g_error_free (error);
		return;
	}

	state = tp_account_manager_get_most_available_presence (priv->manager,
		&status, &status_message);

	idle_presence_changed_cb (account_manager, state, status,
		status_message, idle);

	accounts = tp_account_manager_get_valid_accounts (priv->manager);
	for (l = accounts; l != NULL; l = l->next) {
		tp_g_signal_connect_object (l->data, "status-changed",
					     G_CALLBACK (account_status_changed_cb),
					     idle, 0);
	}
	g_list_free (accounts);

	g_free (status);
	g_free (status_message);
}

static void
empathy_presence_manager_init (EmpathyPresenceManager *idle)
{
	EmpathyPresenceManagerPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (idle,
		EMPATHY_TYPE_PRESENCE_MANAGER, EmpathyPresenceManagerPriv);
	TpDBusDaemon *dbus;

	idle->priv = priv;
	priv->is_idle = FALSE;

	priv->manager = tp_account_manager_dup ();

	tp_account_manager_prepare_async (priv->manager, NULL,
	    account_manager_ready_cb, idle);

	priv->idle_presence_changed_id = g_signal_connect (priv->manager,
		"most-available-presence-changed",
		G_CALLBACK (idle_presence_changed_cb), idle);

	dbus = tp_dbus_daemon_dup (NULL);

	priv->gs_proxy = dbus_g_proxy_new_for_name (
						    tp_proxy_get_dbus_connection (dbus),
						    "org.gnome.SessionManager",
						    "/org/gnome/SessionManager/Presence",
						    "org.gnome.SessionManager.Presence");
	if (priv->gs_proxy) {
		dbus_g_proxy_add_signal (priv->gs_proxy, "StatusChanged",
					 G_TYPE_UINT, G_TYPE_INVALID);
		dbus_g_proxy_connect_signal (priv->gs_proxy, "StatusChanged",
					     G_CALLBACK (idle_session_status_changed_cb),
					     idle, NULL);
	} else {
		DEBUG ("Failed to get gs proxy");
	}

	g_object_unref (dbus);

	priv->connectivity = empathy_connectivity_dup_singleton ();
	priv->state_change_signal_id = g_signal_connect (priv->connectivity,
	    "state-change", G_CALLBACK (idle_state_change_cb), idle);

	priv->connect_times = g_hash_table_new (g_direct_hash, g_direct_equal);
}

EmpathyPresenceManager *
empathy_presence_manager_dup_singleton (void)
{
	return g_object_new (EMPATHY_TYPE_PRESENCE_MANAGER, NULL);
}

TpConnectionPresenceType
empathy_presence_manager_get_state (EmpathyPresenceManager *idle)
{
	EmpathyPresenceManagerPriv *priv;

	priv = GET_PRIV (idle);

	if (G_UNLIKELY (!priv->ready))
		g_critical (G_STRLOC ": %s called before AccountManager ready",
				G_STRFUNC);

	return priv->state;
}

void
empathy_presence_manager_set_state (EmpathyPresenceManager *idle,
			TpConnectionPresenceType   state)
{
	EmpathyPresenceManagerPriv *priv;

	priv = GET_PRIV (idle);

	empathy_presence_manager_set_presence (idle, state, priv->status);
}

void
empathy_presence_manager_set_status (EmpathyPresenceManager *idle,
			 const gchar *status)
{
	EmpathyPresenceManagerPriv *priv;

	priv = GET_PRIV (idle);

	empathy_presence_manager_set_presence (idle, priv->state, status);
}

static void
empathy_presence_manager_do_set_presence (EmpathyPresenceManager *idle,
			   TpConnectionPresenceType status_type,
			   const gchar *status_message)
{
	EmpathyPresenceManagerPriv *priv = GET_PRIV (idle);
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
	tp_account_manager_set_all_requested_presences (priv->manager,
		status_type, status, status_message);
}

void
empathy_presence_manager_set_presence (EmpathyPresenceManager *idle,
			   TpConnectionPresenceType   state,
			   const gchar *status)
{
	EmpathyPresenceManagerPriv *priv;
	const gchar     *default_status;

	priv = GET_PRIV (idle);

	DEBUG ("Changing presence to %s (%d)", status, state);

	g_free (priv->requested_status_message);
	priv->requested_presence_type = state;
	priv->requested_status_message = g_strdup (status);

	/* Do not set translated default messages */
	default_status = empathy_presence_get_default_message (state);
	if (!tp_strdiff (status, default_status)) {
		status = NULL;
	}

	if (state != TP_CONNECTION_PRESENCE_TYPE_OFFLINE &&
			!empathy_connectivity_is_online (priv->connectivity)) {
		DEBUG ("Empathy is not online");

		priv->saved_state = state;
		if (tp_strdiff (priv->status, status)) {
			g_free (priv->saved_status);
			priv->saved_status = NULL;
			if (!EMP_STR_EMPTY (status)) {
				priv->saved_status = g_strdup (status);
			}
		}
		return;
	}

	empathy_presence_manager_do_set_presence (idle, state, status);
}

gboolean
empathy_presence_manager_get_auto_away (EmpathyPresenceManager *idle)
{
	EmpathyPresenceManagerPriv *priv = GET_PRIV (idle);

	return priv->auto_away;
}

void
empathy_presence_manager_set_auto_away (EmpathyPresenceManager *idle,
			    gboolean     auto_away)
{
	EmpathyPresenceManagerPriv *priv = GET_PRIV (idle);

	priv->auto_away = auto_away;

	g_object_notify (G_OBJECT (idle), "auto-away");
}

TpConnectionPresenceType
empathy_presence_manager_get_requested_presence (EmpathyPresenceManager *idle,
    gchar **status,
    gchar **status_message)
{
	EmpathyPresenceManagerPriv *priv = GET_PRIV (idle);

	if (status != NULL) {
		*status = g_strdup (presence_type_to_status[priv->requested_presence_type]);
	}

	if (status_message != NULL) {
		*status_message = g_strdup (priv->requested_status_message);
	}

	return priv->requested_presence_type;
}

/* This function returns %TRUE if EmpathyPresenceManager considers the account
 * @account as having just connected recently. Otherwise, it returns
 * %FALSE. In doubt, %FALSE is returned. */
gboolean
empathy_presence_manager_account_is_just_connected (EmpathyPresenceManager *idle,
					TpAccount *account)
{
	EmpathyPresenceManagerPriv *priv = GET_PRIV (idle);
	GTimeVal val;
	gpointer ptr;
	glong t;

	if (tp_account_get_connection_status (account, NULL)
	    != TP_CONNECTION_STATUS_CONNECTED) {
		return FALSE;
	}

	ptr = g_hash_table_lookup (priv->connect_times, account);

	if (ptr == NULL) {
		return FALSE;
	}

	t = GPOINTER_TO_INT (ptr);

	g_get_current_time (&val);

	return (val.tv_sec - t) < ACCOUNT_IS_JUST_CONNECTED_SECONDS;
}
