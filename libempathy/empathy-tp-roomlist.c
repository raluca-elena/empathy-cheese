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

#include <config.h>

#include <string.h>

#include <telepathy-glib/channel.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/interfaces.h>

#include "empathy-tp-roomlist.h"
#include "empathy-chatroom.h"
#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_TP
#include "empathy-debug.h"

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyTpRoomlist)
typedef struct {
	TpConnection *connection;
	TpChannel    *channel;
	TpAccount    *account;
	gboolean      is_listing;
	gboolean      start_requested;
} EmpathyTpRoomlistPriv;

enum {
	NEW_ROOM,
	DESTROY,
	ERROR,
	LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_ACCOUNT,
	PROP_IS_LISTING,
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (EmpathyTpRoomlist, empathy_tp_roomlist, G_TYPE_OBJECT);

static void
tp_roomlist_listing_cb (TpChannel *channel,
			gboolean   listing,
			gpointer   user_data,
			GObject   *list)
{
	EmpathyTpRoomlistPriv *priv = GET_PRIV (list);

	DEBUG ("Listing: %s", listing ? "Yes" : "No");
	priv->is_listing = listing;
	g_object_notify (list, "is-listing");
}

static void
tp_roomlist_chatrooms_free (gpointer data)
{
	GSList *chatrooms = data;

	g_slist_foreach (chatrooms, (GFunc) g_object_unref, NULL);
	g_slist_free (chatrooms);
}

static void
tp_roomlist_inspect_handles_cb (TpConnection *connection,
				const gchar **names,
				const GError *error,
				gpointer      user_data,
				GObject      *list)
{
	GSList *chatrooms = user_data;

	if (error != NULL) {
		DEBUG ("Error: %s", error->message);
		return;
	}

	while (*names != NULL) {
		EmpathyChatroom *chatroom = chatrooms->data;

		empathy_chatroom_set_room (chatroom, *names);
		g_signal_emit (list, signals[NEW_ROOM], 0, chatroom);

		names++;
		chatrooms = chatrooms->next;
	}
}

static void
tp_roomlist_got_rooms_cb (TpChannel       *channel,
			  const GPtrArray *rooms,
			  gpointer         user_data,
			  GObject         *list)
{
	EmpathyTpRoomlistPriv *priv = GET_PRIV (list);
	EmpathyChatroom       *chatroom;
	guint                  i;
	GArray                *handles = NULL;
	GSList                *chatrooms = NULL;

	for (i = 0; i < rooms->len; i++) {
		const GValue *room_name_value;
		const GValue *handle_name_value;
		const GValue *room_members_value;
		const GValue *room_subject_value;
		const GValue *room_invite_value;
		const GValue *room_password_value;
		GValueArray  *room_struct;
		guint         handle;
		const gchar  *channel_type;
		GHashTable   *info;

		/* Get information */
		room_struct = g_ptr_array_index (rooms, i);
		handle = g_value_get_uint (g_value_array_get_nth (room_struct, 0));
		channel_type = g_value_get_string (g_value_array_get_nth (room_struct, 1));
		info = g_value_get_boxed (g_value_array_get_nth (room_struct, 2));
		room_name_value = g_hash_table_lookup (info, "name");
		handle_name_value = g_hash_table_lookup (info, "handle-name");
		room_subject_value = g_hash_table_lookup (info, "subject");
		room_members_value = g_hash_table_lookup (info, "members");
		room_invite_value = g_hash_table_lookup (info, "invite-only");
		room_password_value = g_hash_table_lookup (info, "password");

		if (tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_TEXT)) {
			continue;
		}

		chatroom = empathy_chatroom_new (priv->account);

		if (room_name_value != NULL) {
			empathy_chatroom_set_name (chatroom,
						   g_value_get_string (room_name_value));
		}

		if (room_members_value != NULL) {
			empathy_chatroom_set_members_count (chatroom,
						   g_value_get_uint (room_members_value));
		}

		if (room_subject_value != NULL) {
			empathy_chatroom_set_subject (chatroom,
						   g_value_get_string (room_subject_value));
		}

		if (room_invite_value != NULL) {
			empathy_chatroom_set_invite_only (chatroom,
						   g_value_get_boolean (room_invite_value));
		}

		if (room_password_value != NULL) {
			empathy_chatroom_set_need_password (chatroom,
						   g_value_get_boolean (room_password_value));
		}

		if (handle_name_value != NULL) {
			empathy_chatroom_set_room (chatroom,
						   g_value_get_string (handle_name_value));

			/* We have the room ID, we can directly emit it */
			g_signal_emit (list, signals[NEW_ROOM], 0, chatroom);
			g_object_unref (chatroom);
		} else {
			/* We don't have the room ID, we'll inspect all handles
			 * at once and then emit rooms */
			if (handles == NULL) {
				handles = g_array_new (FALSE, FALSE, sizeof (guint));
			}

			g_array_append_val (handles, handle);
			chatrooms = g_slist_prepend (chatrooms, chatroom);
		}
	}

	if (handles != NULL) {
		chatrooms = g_slist_reverse (chatrooms);
		tp_cli_connection_call_inspect_handles (priv->connection, -1,
						       TP_HANDLE_TYPE_ROOM,
						       handles,
						       tp_roomlist_inspect_handles_cb,
						       chatrooms,
						       tp_roomlist_chatrooms_free,
						       list);
		g_array_unref (handles);
	}
}

static void
tp_roomlist_get_listing_rooms_cb (TpChannel    *channel,
				  gboolean      is_listing,
				  const GError *error,
				  gpointer      user_data,
				  GObject      *list)
{
	EmpathyTpRoomlistPriv *priv = GET_PRIV (list);

	if (error) {
		DEBUG ("Error geting listing rooms: %s", error->message);
		return;
	}

	priv->is_listing = is_listing;
	g_object_notify (list, "is-listing");
}

static void
tp_roomlist_invalidated_cb (TpChannel         *channel,
			    guint              domain,
			    gint               code,
			    gchar             *message,
			    EmpathyTpRoomlist *list)
{
	DEBUG ("Channel invalidated: %s", message);
	g_signal_emit (list, signals[DESTROY], 0);
}

static void
call_list_rooms_cb (TpChannel *proxy,
		    const GError *error,
		    gpointer list,
		    GObject *weak_object)
{
	if (error != NULL) {
		DEBUG ("Error listing rooms: %s", error->message);
		g_signal_emit_by_name (list, "error::start", error);
	}
}

static void
stop_listing_cb (TpChannel *proxy,
		 const GError *error,
		 gpointer list,
		 GObject *weak_object)
{
	if (error != NULL) {
		DEBUG ("Error on stop listing: %s", error->message);
		g_signal_emit_by_name (list, "error::stop", error);
	}
}

static void
tp_roomlist_create_channel_cb (GObject *source,
				GAsyncResult *result,
				gpointer      user_data)
{
	EmpathyTpRoomlist *self = user_data;
	EmpathyTpRoomlistPriv *priv = GET_PRIV (self);
	GError *error = NULL;

	priv->channel = tp_account_channel_request_create_and_handle_channel_finish (
		TP_ACCOUNT_CHANNEL_REQUEST (source), result, NULL, &error);

	if (priv->channel == NULL) {
		DEBUG ("Error creating channel: %s", error->message);
		g_error_free (error);
		goto out;
	}

	g_signal_connect (priv->channel, "invalidated",
			  G_CALLBACK (tp_roomlist_invalidated_cb), self);

	tp_cli_channel_type_room_list_connect_to_listing_rooms (priv->channel,
								tp_roomlist_listing_cb,
								NULL, NULL,
								G_OBJECT (self),
								NULL);
	tp_cli_channel_type_room_list_connect_to_got_rooms (priv->channel,
							    tp_roomlist_got_rooms_cb,
							    NULL, NULL,
							    G_OBJECT (self),
							    NULL);

	tp_cli_channel_type_room_list_call_get_listing_rooms (priv->channel, -1,
							      tp_roomlist_get_listing_rooms_cb,
							      NULL, NULL,
							      G_OBJECT (self));

	if (priv->start_requested == TRUE) {
		tp_cli_channel_type_room_list_call_list_rooms (priv->channel, -1,
			call_list_rooms_cb, self, NULL, G_OBJECT (self));
		priv->start_requested = FALSE;
	}

out:
	g_object_unref (self);
}

static void
tp_roomlist_finalize (GObject *object)
{
	EmpathyTpRoomlistPriv *priv = GET_PRIV (object);

	if (priv->channel) {
		DEBUG ("Closing channel...");
		g_signal_handlers_disconnect_by_func (priv->channel,
						      tp_roomlist_invalidated_cb,
						      object);
		tp_cli_channel_call_close (priv->channel, -1,
					   NULL, NULL, NULL, NULL);
		g_object_unref (priv->channel);
	}

	if (priv->account) {
		g_object_unref (priv->account);
	}
	if (priv->connection) {
		g_object_unref (priv->connection);
	}

	G_OBJECT_CLASS (empathy_tp_roomlist_parent_class)->finalize (object);
}

static void
tp_roomlist_constructed (GObject *list)
{
	EmpathyTpRoomlistPriv *priv = GET_PRIV (list);
	GHashTable *request;
	TpAccountChannelRequest *req;

	request = tp_asv_new (
		TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
		  TP_IFACE_CHANNEL_TYPE_ROOM_LIST,
		TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_NONE,
		NULL);

	priv->connection = tp_account_get_connection (priv->account);
	g_object_ref (priv->connection);

	req = tp_account_channel_request_new (priv->account, request,
		TP_USER_ACTION_TIME_CURRENT_TIME);

	/* Ensure we stay alive during the async call */
	g_object_ref (list);

	tp_account_channel_request_create_and_handle_channel_async (req, NULL,
		tp_roomlist_create_channel_cb, list);

	g_hash_table_unref (request);
	g_object_unref (req);
}

static void
tp_roomlist_get_property (GObject    *object,
			  guint       param_id,
			  GValue     *value,
			  GParamSpec *pspec)
{
	EmpathyTpRoomlistPriv *priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_ACCOUNT:
		g_value_set_object (value, priv->account);
		break;
	case PROP_IS_LISTING:
		g_value_set_boolean (value, priv->is_listing);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
tp_roomlist_set_property (GObject      *object,
			  guint         param_id,
			  const GValue *value,
			  GParamSpec   *pspec)
{
	EmpathyTpRoomlistPriv *priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_ACCOUNT:
		priv->account = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
empathy_tp_roomlist_class_init (EmpathyTpRoomlistClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tp_roomlist_finalize;
	object_class->constructed = tp_roomlist_constructed;
	object_class->get_property = tp_roomlist_get_property;
	object_class->set_property = tp_roomlist_set_property;

	g_object_class_install_property (object_class,
					 PROP_ACCOUNT,
					 g_param_spec_object ("account",
							      "The Account",
							      "The account on which it lists rooms",
							      TP_TYPE_ACCOUNT,
							      G_PARAM_READWRITE |
							      G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_IS_LISTING,
					 g_param_spec_boolean ("is-listing",
							       "Is listing",
							       "Are we listing rooms",
							       FALSE,
							       G_PARAM_READABLE));

	signals[NEW_ROOM] =
		g_signal_new ("new-room",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_generic,
			      G_TYPE_NONE,
			      1, EMPATHY_TYPE_CHATROOM);

	signals[DESTROY] =
		g_signal_new ("destroy",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_generic,
			      G_TYPE_NONE,
			      0);

	signals[ERROR] =
		g_signal_new ("error",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_generic,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

	g_type_class_add_private (object_class, sizeof (EmpathyTpRoomlistPriv));
}

static void
empathy_tp_roomlist_init (EmpathyTpRoomlist *list)
{
	EmpathyTpRoomlistPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (list,
		EMPATHY_TYPE_TP_ROOMLIST, EmpathyTpRoomlistPriv);

	list->priv = priv;
	priv->start_requested = FALSE;
	priv->is_listing = FALSE;
}

EmpathyTpRoomlist *
empathy_tp_roomlist_new (TpAccount *account)
{
	EmpathyTpRoomlist *list;

	list = g_object_new (EMPATHY_TYPE_TP_ROOMLIST,
			     "account", account,
			     NULL);

	return list;
}

gboolean
empathy_tp_roomlist_is_listing (EmpathyTpRoomlist *list)
{
	EmpathyTpRoomlistPriv *priv = GET_PRIV (list);

	g_return_val_if_fail (EMPATHY_IS_TP_ROOMLIST (list), FALSE);

	return priv->is_listing;
}

void
empathy_tp_roomlist_start (EmpathyTpRoomlist *list)
{
	EmpathyTpRoomlistPriv *priv = GET_PRIV (list);

	g_return_if_fail (EMPATHY_IS_TP_ROOMLIST (list));
	if (priv->channel != NULL) {
		tp_cli_channel_type_room_list_call_list_rooms (priv->channel, -1,
			call_list_rooms_cb, list, NULL, NULL);
	} else {
		priv->start_requested = TRUE;
	}
}

void
empathy_tp_roomlist_stop (EmpathyTpRoomlist *list)
{
	EmpathyTpRoomlistPriv *priv = GET_PRIV (list);

	g_return_if_fail (EMPATHY_IS_TP_ROOMLIST (list));

	if (priv->channel == NULL)
		return;

	g_return_if_fail (TP_IS_CHANNEL (priv->channel));

	tp_cli_channel_type_room_list_call_stop_listing (priv->channel, -1,
							 stop_listing_cb, list, NULL, NULL);
}

