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

#include <telepathy-glib/telepathy-glib.h>

#include <extensions/extensions.h>

#include "empathy-tp-chat.h"
#include "empathy-tp-contact-factory.h"
#include "empathy-contact-list.h"
#include "empathy-marshal.h"
#include "empathy-request-util.h"
#include "empathy-time.h"
#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_TP | EMPATHY_DEBUG_CHAT
#include "empathy-debug.h"

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyTpChat)
typedef struct {
	gboolean               dispose_has_run;
	TpAccount             *account;
	TpConnection          *connection;
	EmpathyContact        *user;
	EmpathyContact        *remote_contact;
	GList                 *members;
	TpChannel             *channel;
	/* Queue of messages not signalled yet */
	GQueue                *messages_queue;
	/* Queue of messages signalled but not acked yet */
	GQueue                *pending_messages_queue;
	gboolean               had_properties_list;
	GPtrArray             *properties;
	TpChannelPasswordFlags password_flags;
	/* TRUE if we fetched the password flag of the channel or if it's not needed
	 * (channel doesn't implement the Password interface) */
	gboolean               got_password_flags;
	gboolean               ready;
	gboolean               can_upgrade_to_muc;
} EmpathyTpChatPriv;

static void tp_chat_iface_init         (EmpathyContactListIface *iface);

enum {
	PROP_0,
	PROP_ACCOUNT,
	PROP_CHANNEL,
	PROP_REMOTE_CONTACT,
	PROP_PASSWORD_NEEDED,
	PROP_READY,
};

enum {
	MESSAGE_RECEIVED,
	SEND_ERROR,
	CHAT_STATE_CHANGED,
	PROPERTY_CHANGED,
	DESTROY,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE (EmpathyTpChat, empathy_tp_chat, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (EMPATHY_TYPE_CONTACT_LIST,
						tp_chat_iface_init));

static void acknowledge_messages (EmpathyTpChat *chat, GArray *ids);

static void
tp_chat_invalidated_cb (TpProxy       *proxy,
			guint          domain,
			gint           code,
			gchar         *message,
			EmpathyTpChat *chat)
{
	DEBUG ("Channel invalidated: %s", message);
	g_signal_emit (chat, signals[DESTROY], 0);
}

static void
tp_chat_async_cb (TpChannel *proxy,
		  const GError *error,
		  gpointer user_data,
		  GObject *weak_object)
{
	if (error) {
		DEBUG ("Error %s: %s", (gchar *) user_data, error->message);
	}
}

static void
create_conference_cb (GObject *source,
		      GAsyncResult *result,
		      gpointer user_data)
{
	GError *error = NULL;

	if (!tp_account_channel_request_create_channel_finish (
			TP_ACCOUNT_CHANNEL_REQUEST (source), result, &error)) {
		DEBUG ("Failed to create conference channel: %s", error->message);
		g_error_free (error);
	}
}

static void
tp_chat_add (EmpathyContactList *list,
	     EmpathyContact     *contact,
	     const gchar        *message)
{
	EmpathyTpChatPriv *priv = GET_PRIV (list);

	if (tp_proxy_has_interface_by_id (priv->channel,
		TP_IFACE_QUARK_CHANNEL_INTERFACE_GROUP)) {
		TpHandle           handle;
		GArray             handles = {(gchar *) &handle, 1};

		g_return_if_fail (EMPATHY_IS_TP_CHAT (list));
		g_return_if_fail (EMPATHY_IS_CONTACT (contact));

		handle = empathy_contact_get_handle (contact);
		tp_cli_channel_interface_group_call_add_members (priv->channel,
			-1, &handles, NULL, NULL, NULL, NULL, NULL);
	} else if (priv->can_upgrade_to_muc) {
		TpAccountChannelRequest *req;
		GHashTable        *props;
		const char        *object_path;
		GPtrArray          channels = { (gpointer *) &object_path, 1 };
		const char        *invitees[2] = { NULL, };

		invitees[0] = empathy_contact_get_id (contact);
		object_path = tp_proxy_get_object_path (priv->channel);

		props = tp_asv_new (
		    TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
		        TP_IFACE_CHANNEL_TYPE_TEXT,
		    TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
		        TP_HANDLE_TYPE_NONE,
		    TP_PROP_CHANNEL_INTERFACE_CONFERENCE_INITIAL_CHANNELS,
		        TP_ARRAY_TYPE_OBJECT_PATH_LIST, &channels,
		    TP_PROP_CHANNEL_INTERFACE_CONFERENCE_INITIAL_INVITEE_IDS,
		        G_TYPE_STRV, invitees,
		    /* FIXME: InvitationMessage ? */
		    NULL);

		req = tp_account_channel_request_new (priv->account, props,
			TP_USER_ACTION_TIME_NOT_USER_ACTION);

		/* Although this is a MUC, it's anonymous, so CreateChannel is
		 * valid. */
		tp_account_channel_request_create_channel_async (req, EMPATHY_CHAT_BUS_NAME,
			NULL, create_conference_cb, NULL);

		g_object_unref (req);
		g_hash_table_unref (props);
	} else {
		g_warning ("Cannot add to this channel");
	}
}

static void
tp_chat_remove (EmpathyContactList *list,
		EmpathyContact     *contact,
		const gchar        *message)
{
	EmpathyTpChatPriv *priv = GET_PRIV (list);
	TpHandle           handle;
	GArray             handles = {(gchar *) &handle, 1};

	g_return_if_fail (EMPATHY_IS_TP_CHAT (list));
	g_return_if_fail (EMPATHY_IS_CONTACT (contact));

	handle = empathy_contact_get_handle (contact);
	tp_cli_channel_interface_group_call_remove_members (priv->channel, -1,
							    &handles, NULL,
							    NULL, NULL, NULL,
							    NULL);
}

static GList *
tp_chat_get_members (EmpathyContactList *list)
{
	EmpathyTpChatPriv *priv = GET_PRIV (list);
	GList             *members = NULL;

	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (list), NULL);

	if (priv->members) {
		members = g_list_copy (priv->members);
		g_list_foreach (members, (GFunc) g_object_ref, NULL);
	} else {
		members = g_list_prepend (members, g_object_ref (priv->user));
		if (priv->remote_contact != NULL)
			members = g_list_prepend (members, g_object_ref (priv->remote_contact));
	}

	return members;
}

static void
check_ready (EmpathyTpChat *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	if (priv->ready)
		return;

	if (g_queue_get_length (priv->messages_queue) > 0)
		return;

	DEBUG ("Ready");

	priv->ready = TRUE;
	g_object_notify (G_OBJECT (chat), "ready");
}

static void
tp_chat_emit_queued_messages (EmpathyTpChat *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);
	EmpathyMessage    *message;

	/* Check if we can now emit some queued messages */
	while ((message = g_queue_peek_head (priv->messages_queue)) != NULL) {
		if (empathy_message_get_sender (message) == NULL) {
			break;
		}

		DEBUG ("Queued message ready");
		g_queue_pop_head (priv->messages_queue);
		g_queue_push_tail (priv->pending_messages_queue, message);
		g_signal_emit (chat, signals[MESSAGE_RECEIVED], 0, message);
	}

	check_ready (chat);
}

static void
tp_chat_got_sender_cb (TpConnection            *connection,
		       EmpathyContact          *contact,
		       const GError            *error,
		       gpointer                 message,
		       GObject                 *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	if (error) {
		DEBUG ("Error: %s", error->message);
		/* Do not block the message queue, just drop this message */
		g_queue_remove (priv->messages_queue, message);
	} else {
		empathy_message_set_sender (message, contact);
	}

	tp_chat_emit_queued_messages (EMPATHY_TP_CHAT (chat));
}

static void
tp_chat_build_message (EmpathyTpChat *chat,
		       TpMessage     *msg,
		       gboolean       incoming)
{
	EmpathyTpChatPriv *priv;
	EmpathyMessage    *message;
	TpContact *sender;

	priv = GET_PRIV (chat);

	message = empathy_message_new_from_tp_message (msg, incoming);
	/* FIXME: this is actually a lie for incoming messages. */
	empathy_message_set_receiver (message, priv->user);

	g_queue_push_tail (priv->messages_queue, message);

	sender = tp_signalled_message_get_sender (msg);
	g_assert (sender != NULL);

	if (tp_contact_get_handle (sender) == 0) {
		empathy_message_set_sender (message, priv->user);
		tp_chat_emit_queued_messages (chat);
	} else {
		empathy_tp_contact_factory_get_from_handle (priv->connection,
			tp_contact_get_handle (sender),
			tp_chat_got_sender_cb,
			message, NULL, G_OBJECT (chat));
	}
}

static void
message_received_cb (TpTextChannel   *channel,
		     TpMessage *message,
		     EmpathyTpChat *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);
	gchar *message_body;

	message_body = tp_message_to_text (message, NULL);

	DEBUG ("Message received from channel %s: %s",
		tp_proxy_get_object_path (channel), message_body);

	if (message_body == NULL) {
		DEBUG ("Empty message with NonTextContent, ignoring and acking.");

		tp_text_channel_ack_message_async (TP_TEXT_CHANNEL (priv->channel),
			message, NULL, NULL);
		return;
	}

	tp_chat_build_message (chat, message, TRUE);

	g_free (message_body);
}

static void
message_sent_cb (TpTextChannel   *channel,
		 TpMessage *message,
		 TpMessageSendingFlags flags,
		 gchar              *token,
		 EmpathyTpChat      *chat)
{
	gchar *message_body;

	message_body = tp_message_to_text (message, NULL);

	DEBUG ("Message sent: %s", message_body);

	tp_chat_build_message (chat, message, FALSE);

	g_free (message_body);
}

static void
tp_chat_send_error_cb (TpChannel   *channel,
		       guint        error_code,
		       guint        timestamp,
		       guint        message_type,
		       const gchar *message_body,
		       gpointer     user_data,
		       GObject     *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	if (priv->channel == NULL)
		return;

	DEBUG ("Error sending '%s' (%d)", message_body, error_code);

	g_signal_emit (chat, signals[SEND_ERROR], 0, message_body, error_code);
}

static TpChannelTextSendError
error_to_text_send_error (GError *error)
{
	if (error->domain != TP_ERRORS)
		return TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN;

	switch (error->code) {
		case TP_ERROR_OFFLINE:
			return TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE;
		case TP_ERROR_INVALID_HANDLE:
			return TP_CHANNEL_TEXT_SEND_ERROR_INVALID_CONTACT;
		case TP_ERROR_PERMISSION_DENIED:
			return TP_CHANNEL_TEXT_SEND_ERROR_PERMISSION_DENIED;
		case TP_ERROR_NOT_IMPLEMENTED:
			return TP_CHANNEL_TEXT_SEND_ERROR_NOT_IMPLEMENTED;
	}

	return TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN;
}

static void
message_send_cb (GObject *source,
		 GAsyncResult *result,
		 gpointer      user_data)
{
	EmpathyTpChat *chat = user_data;
	TpTextChannel *channel = (TpTextChannel *) source;
	GError *error = NULL;

	if (!tp_text_channel_send_message_finish (channel, result, NULL, &error)) {
		DEBUG ("Error: %s", error->message);

		/* FIXME: we should use the body of the message as first argument of the
		 * signal but can't easily get it as we just get a user_data pointer. Once
		 * we'll have rebased EmpathyTpChat on top of TpTextChannel we'll be able
		 * to use the user_data pointer to pass the message and fix this. */
		g_signal_emit (chat, signals[SEND_ERROR], 0,
			       NULL, error_to_text_send_error (error));

		g_error_free (error);
	}
}

typedef struct {
	EmpathyTpChat *chat;
	TpChannelChatState state;
} StateChangedData;

static void
tp_chat_state_changed_got_contact_cb (TpConnection            *connection,
				      EmpathyContact          *contact,
				      const GError            *error,
				      gpointer                 user_data,
				      GObject                 *chat)
{
	TpChannelChatState state;

	if (error) {
		DEBUG ("Error: %s", error->message);
		return;
	}

	state = GPOINTER_TO_UINT (user_data);
	DEBUG ("Chat state changed for %s (%d): %d",
		empathy_contact_get_alias (contact),
		empathy_contact_get_handle (contact), state);

	g_signal_emit (chat, signals[CHAT_STATE_CHANGED], 0, contact, state);
}

static void
tp_chat_state_changed_cb (TpChannel *channel,
			  TpHandle   handle,
			  TpChannelChatState state,
			  gpointer   user_data,
			  GObject   *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	empathy_tp_contact_factory_get_from_handle (priv->connection, handle,
		tp_chat_state_changed_got_contact_cb, GUINT_TO_POINTER (state),
		NULL, chat);
}

static void
list_pending_messages (EmpathyTpChat *self)
{
	EmpathyTpChatPriv *priv = GET_PRIV (self);
	GList *messages, *l;

	g_assert (priv->channel != NULL);

	messages = tp_text_channel_get_pending_messages (
		TP_TEXT_CHANNEL (priv->channel));

	for (l = messages; l != NULL; l = g_list_next (l)) {
		TpMessage *message = l->data;
		gchar          *message_body;

		message_body = tp_message_to_text (message, NULL);

		DEBUG ("Message pending: %s", message_body);

		if (message_body == NULL) {
			DEBUG ("Empty message with NonTextContent, ignoring and acking.");

			tp_text_channel_ack_message_async (TP_TEXT_CHANNEL (priv->channel),
				message, NULL, NULL);
			continue;
		}

		tp_chat_build_message (self, message, TRUE);

		g_free (message_body);
	}

	g_list_free (messages);
}

static void
tp_chat_property_flags_changed_cb (TpProxy         *proxy,
				   const GPtrArray *properties,
				   gpointer         user_data,
				   GObject         *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);
	guint              i, j;

	if (priv->channel == NULL)
		return;

	if (!priv->had_properties_list || !properties) {
		return;
	}

	for (i = 0; i < properties->len; i++) {
		GValueArray           *prop_struct;
		EmpathyTpChatProperty *property;
		guint                  id;
		guint                  flags;

		prop_struct = g_ptr_array_index (properties, i);
		id = g_value_get_uint (g_value_array_get_nth (prop_struct, 0));
		flags = g_value_get_uint (g_value_array_get_nth (prop_struct, 1));

		for (j = 0; j < priv->properties->len; j++) {
			property = g_ptr_array_index (priv->properties, j);
			if (property->id == id) {
				property->flags = flags;
				DEBUG ("property %s flags changed: %d",
					property->name, property->flags);
				break;
			}
		}
	}
}

static void
tp_chat_properties_changed_cb (TpProxy         *proxy,
			       const GPtrArray *properties,
			       gpointer         user_data,
			       GObject         *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);
	guint              i, j;

	if (priv->channel == NULL)
		return;

	if (!priv->had_properties_list || !properties) {
		return;
	}

	for (i = 0; i < properties->len; i++) {
		GValueArray           *prop_struct;
		EmpathyTpChatProperty *property;
		guint                  id;
		GValue                *src_value;

		prop_struct = g_ptr_array_index (properties, i);
		id = g_value_get_uint (g_value_array_get_nth (prop_struct, 0));
		src_value = g_value_get_boxed (g_value_array_get_nth (prop_struct, 1));

		for (j = 0; j < priv->properties->len; j++) {
			property = g_ptr_array_index (priv->properties, j);
			if (property->id == id) {
				if (property->value) {
					g_value_copy (src_value, property->value);
				} else {
					property->value = tp_g_value_slice_dup (src_value);
				}

				DEBUG ("property %s changed", property->name);
				g_signal_emit (chat, signals[PROPERTY_CHANGED], 0,
					       property->name, property->value);
				break;
			}
		}
	}
}

static void
tp_chat_get_properties_cb (TpProxy         *proxy,
			   const GPtrArray *properties,
			   const GError    *error,
			   gpointer         user_data,
			   GObject         *chat)
{
	if (error) {
		DEBUG ("Error getting properties: %s", error->message);
		return;
	}

	tp_chat_properties_changed_cb (proxy, properties, user_data, chat);
}

static void
tp_chat_list_properties_cb (TpProxy         *proxy,
			    const GPtrArray *properties,
			    const GError    *error,
			    gpointer         user_data,
			    GObject         *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);
	GArray            *ids;
	guint              i;

	if (priv->channel == NULL)
		return;

	priv->had_properties_list = TRUE;

	if (error) {
		DEBUG ("Error listing properties: %s", error->message);
		return;
	}

	ids = g_array_sized_new (FALSE, FALSE, sizeof (guint), properties->len);
	priv->properties = g_ptr_array_sized_new (properties->len);
	for (i = 0; i < properties->len; i++) {
		GValueArray           *prop_struct;
		EmpathyTpChatProperty *property;

		prop_struct = g_ptr_array_index (properties, i);
		property = g_slice_new0 (EmpathyTpChatProperty);
		property->id = g_value_get_uint (g_value_array_get_nth (prop_struct, 0));
		property->name = g_value_dup_string (g_value_array_get_nth (prop_struct, 1));
		property->flags = g_value_get_uint (g_value_array_get_nth (prop_struct, 3));

		DEBUG ("Adding property name=%s id=%d flags=%d",
			property->name, property->id, property->flags);
		g_ptr_array_add (priv->properties, property);
		if (property->flags & TP_PROPERTY_FLAG_READ) {
			g_array_append_val (ids, property->id);
		}
	}

	tp_cli_properties_interface_call_get_properties (proxy, -1,
							 ids,
							 tp_chat_get_properties_cb,
							 NULL, NULL,
							 chat);

	g_array_free (ids, TRUE);
}

void
empathy_tp_chat_set_property (EmpathyTpChat *chat,
			      const gchar   *name,
			      const GValue  *value)
{
	EmpathyTpChatPriv     *priv = GET_PRIV (chat);
	EmpathyTpChatProperty *property;
	guint                  i;

	if (!priv->had_properties_list) {
		return;
	}

	for (i = 0; i < priv->properties->len; i++) {
		property = g_ptr_array_index (priv->properties, i);
		if (!tp_strdiff (property->name, name)) {
			GPtrArray   *properties;
			GValueArray *prop;
			GValue       id = {0, };
			GValue       dest_value = {0, };

			if (!(property->flags & TP_PROPERTY_FLAG_WRITE)) {
				break;
			}

			g_value_init (&id, G_TYPE_UINT);
			g_value_init (&dest_value, G_TYPE_VALUE);
			g_value_set_uint (&id, property->id);
			g_value_set_boxed (&dest_value, value);

			prop = g_value_array_new (2);
			g_value_array_append (prop, &id);
			g_value_array_append (prop, &dest_value);

			properties = g_ptr_array_sized_new (1);
			g_ptr_array_add (properties, prop);

			DEBUG ("Set property %s", name);
			tp_cli_properties_interface_call_set_properties (priv->channel, -1,
									 properties,
									 (tp_cli_properties_interface_callback_for_set_properties)
									 tp_chat_async_cb,
									 "Seting property", NULL,
									 G_OBJECT (chat));

			g_ptr_array_free (properties, TRUE);
			g_value_array_free (prop);

			break;
		}
	}
}

EmpathyTpChatProperty *
empathy_tp_chat_get_property (EmpathyTpChat *chat,
			      const gchar   *name)
{
	EmpathyTpChatPriv     *priv = GET_PRIV (chat);
	EmpathyTpChatProperty *property;
	guint                  i;

	if (!priv->had_properties_list) {
		return NULL;
	}

	for (i = 0; i < priv->properties->len; i++) {
		property = g_ptr_array_index (priv->properties, i);
		if (!tp_strdiff (property->name, name)) {
			return property;
		}
	}

	return NULL;
}

GPtrArray *
empathy_tp_chat_get_properties (EmpathyTpChat *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	return priv->properties;
}

static void
tp_chat_dispose (GObject *object)
{
	EmpathyTpChat *self = EMPATHY_TP_CHAT (object);
	EmpathyTpChatPriv *priv = GET_PRIV (self);

	if (priv->dispose_has_run)
		return;

	priv->dispose_has_run = TRUE;

	tp_clear_object (&priv->account);

	if (priv->connection != NULL)
		g_object_unref (priv->connection);
	priv->connection = NULL;

	if (priv->channel != NULL) {
		g_signal_handlers_disconnect_by_func (priv->channel,
			tp_chat_invalidated_cb, self);
		g_object_unref (priv->channel);
	}
	priv->channel = NULL;

	if (priv->remote_contact != NULL)
		g_object_unref (priv->remote_contact);
	priv->remote_contact = NULL;

	if (priv->user != NULL)
		g_object_unref (priv->user);
	priv->user = NULL;

	g_queue_foreach (priv->messages_queue, (GFunc) g_object_unref, NULL);
	g_queue_clear (priv->messages_queue);

	g_queue_foreach (priv->pending_messages_queue,
		(GFunc) g_object_unref, NULL);
	g_queue_clear (priv->pending_messages_queue);

	if (G_OBJECT_CLASS (empathy_tp_chat_parent_class)->dispose)
		G_OBJECT_CLASS (empathy_tp_chat_parent_class)->dispose (object);
}

static void
tp_chat_finalize (GObject *object)
{
	EmpathyTpChatPriv *priv = GET_PRIV (object);
	guint              i;

	DEBUG ("Finalize: %p", object);

	if (priv->properties) {
		for (i = 0; i < priv->properties->len; i++) {
			EmpathyTpChatProperty *property;

			property = g_ptr_array_index (priv->properties, i);
			g_free (property->name);
			if (property->value) {
				tp_g_value_slice_free (property->value);
			}
			g_slice_free (EmpathyTpChatProperty, property);
		}
		g_ptr_array_free (priv->properties, TRUE);
	}

	g_queue_free (priv->messages_queue);
	g_queue_free (priv->pending_messages_queue);

	G_OBJECT_CLASS (empathy_tp_chat_parent_class)->finalize (object);
}

static void
check_almost_ready (EmpathyTpChat *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	if (priv->ready)
		return;

	if (priv->user == NULL)
		return;

	if (!priv->got_password_flags)
		return;

	/* We need either the members (room) or the remote contact (private chat).
	 * If the chat is protected by a password we can't get these information so
	 * consider the chat as ready so it can be presented to the user. */
	if (!empathy_tp_chat_password_needed (chat) && priv->members == NULL &&
	    priv->remote_contact == NULL)
		return;

	/* We use the default factory so this feature should have been prepared */
	g_assert (tp_proxy_is_prepared (priv->channel,
		TP_TEXT_CHANNEL_FEATURE_INCOMING_MESSAGES));

	tp_g_signal_connect_object (priv->channel, "message-received",
		G_CALLBACK (message_received_cb), chat, 0);

	list_pending_messages (chat);

	tp_g_signal_connect_object (priv->channel, "message-sent",
		G_CALLBACK (message_sent_cb), chat, 0);

	tp_cli_channel_type_text_connect_to_send_error (priv->channel,
							tp_chat_send_error_cb,
							NULL, NULL,
							G_OBJECT (chat), NULL);
	tp_cli_channel_interface_chat_state_connect_to_chat_state_changed (priv->channel,
									   tp_chat_state_changed_cb,
									   NULL, NULL,
									   G_OBJECT (chat), NULL);

	check_ready (chat);
}

static void
tp_chat_update_remote_contact (EmpathyTpChat *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);
	EmpathyContact *contact = NULL;
	TpHandle self_handle;
	TpHandleType handle_type;
	GList *l;

	/* If this is a named chatroom, never pretend it is a private chat */
	tp_channel_get_handle (priv->channel, &handle_type);
	if (handle_type == TP_HANDLE_TYPE_ROOM) {
		return;
	}

	/* This is an MSN chat, but it's the new style where 1-1 chats don't
	 * have the group interface. If it has the conference interface, then
	 * it is indeed a MUC. */
	if (tp_proxy_has_interface_by_id (priv->channel,
					  TP_IFACE_QUARK_CHANNEL_INTERFACE_CONFERENCE)) {
		return;
	}

	/* This is an MSN-like chat where anyone can join the chat at anytime.
	 * If there is only one non-self contact member, we are in a private
	 * chat and we set the "remote-contact" property to that contact. If
	 * there are more, set the "remote-contact" property to NULL and the
	 * UI will display a contact list. */
	self_handle = tp_channel_group_get_self_handle (priv->channel);
	for (l = priv->members; l; l = l->next) {
		/* Skip self contact if member */
		if (empathy_contact_get_handle (l->data) == self_handle) {
			continue;
		}

		/* We have more than one remote contact, break */
		if (contact != NULL) {
			contact = NULL;
			break;
		}

		/* If we didn't find yet a remote contact, keep this one */
		contact = l->data;
	}

	if (priv->remote_contact == contact) {
		return;
	}

	DEBUG ("Changing remote contact from %p to %p",
		priv->remote_contact, contact);

	if (priv->remote_contact) {
		g_object_unref (priv->remote_contact);
	}

	priv->remote_contact = contact ? g_object_ref (contact) : NULL;
	g_object_notify (G_OBJECT (chat), "remote-contact");
}

static void
tp_chat_got_added_contacts_cb (TpConnection            *connection,
			       guint                    n_contacts,
			       EmpathyContact * const * contacts,
			       guint                    n_failed,
			       const TpHandle          *failed,
			       const GError            *error,
			       gpointer                 user_data,
			       GObject                 *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);
	guint i;
	const TpIntSet *members;
	TpHandle handle;
	EmpathyContact *contact;

	if (error) {
		DEBUG ("Error: %s", error->message);
		return;
	}

	members = tp_channel_group_get_members (priv->channel);
	for (i = 0; i < n_contacts; i++) {
		contact = contacts[i];
		handle = empathy_contact_get_handle (contact);

		/* Make sure the contact is still member */
		if (tp_intset_is_member (members, handle)) {
			priv->members = g_list_prepend (priv->members,
				g_object_ref (contact));
			g_signal_emit_by_name (chat, "members-changed",
					       contact, NULL, 0, NULL, TRUE);
		}
	}

	tp_chat_update_remote_contact (EMPATHY_TP_CHAT (chat));
	check_almost_ready (EMPATHY_TP_CHAT (chat));
}

static EmpathyContact *
chat_lookup_contact (EmpathyTpChat *chat,
		     TpHandle       handle,
		     gboolean       remove_)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);
	GList *l;

	for (l = priv->members; l; l = l->next) {
		EmpathyContact *c = l->data;

		if (empathy_contact_get_handle (c) != handle) {
			continue;
		}

		if (remove_) {
			/* Caller takes the reference. */
			priv->members = g_list_delete_link (priv->members, l);
		} else {
			g_object_ref (c);
		}

		return c;
	}

	return NULL;
}

typedef struct
{
    TpHandle old_handle;
    guint reason;
    gchar *message;
} ContactRenameData;

static ContactRenameData *
contact_rename_data_new (TpHandle handle,
			 guint reason,
			 const gchar* message)
{
	ContactRenameData *data = g_new (ContactRenameData, 1);
	data->old_handle = handle;
	data->reason = reason;
	data->message = g_strdup (message);

	return data;
}

static void
contact_rename_data_free (ContactRenameData* data)
{
	g_free (data->message);
	g_free (data);
}

static void
tp_chat_got_renamed_contacts_cb (TpConnection            *connection,
                                 guint                    n_contacts,
                                 EmpathyContact * const * contacts,
                                 guint                    n_failed,
                                 const TpHandle          *failed,
                                 const GError            *error,
                                 gpointer                 user_data,
                                 GObject                 *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);
	const TpIntSet *members;
	TpHandle handle;
	EmpathyContact *old = NULL, *new = NULL;
	ContactRenameData *rename_data = (ContactRenameData *) user_data;

	if (error) {
		DEBUG ("Error: %s", error->message);
		return;
	}

	/* renamed members can only be delivered one at a time */
	g_warn_if_fail (n_contacts == 1);

	new = contacts[0];

	members = tp_channel_group_get_members (priv->channel);
	handle = empathy_contact_get_handle (new);

	old = chat_lookup_contact (EMPATHY_TP_CHAT (chat),
				   rename_data->old_handle, TRUE);

	/* Make sure the contact is still member */
	if (tp_intset_is_member (members, handle)) {
		priv->members = g_list_prepend (priv->members,
			g_object_ref (new));

		if (old != NULL) {
			g_signal_emit_by_name (chat, "member-renamed",
					       old, new, rename_data->reason,
					       rename_data->message);
			g_object_unref (old);
		}
	}

	if (priv->user == old) {
		/* We change our nick */
		tp_clear_object (&priv->user);
		priv->user = g_object_ref (new);
	}

	tp_chat_update_remote_contact (EMPATHY_TP_CHAT (chat));
	check_almost_ready (EMPATHY_TP_CHAT (chat));
}


static void
tp_chat_group_members_changed_cb (TpChannel     *self,
				  gchar         *message,
				  GArray        *added,
				  GArray        *removed,
				  GArray        *local_pending,
				  GArray        *remote_pending,
				  guint          actor,
				  guint          reason,
				  EmpathyTpChat *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);
	EmpathyContact *contact;
	EmpathyContact *actor_contact = NULL;
	guint i;
	ContactRenameData *rename_data;
	TpHandle old_handle;

	/* Contact renamed */
	if (reason == TP_CHANNEL_GROUP_CHANGE_REASON_RENAMED) {
		/* there can only be a single 'added' and a single 'removed' handle */
		if (removed->len != 1 || added->len != 1) {
			g_warning ("RENAMED with %u added, %u removed (expected 1, 1)",
				added->len, removed->len);
			return;
		}

		old_handle = g_array_index (removed, guint, 0);

		rename_data = contact_rename_data_new (old_handle, reason, message);
		empathy_tp_contact_factory_get_from_handles (priv->connection,
			added->len, (TpHandle *) added->data,
			tp_chat_got_renamed_contacts_cb,
			rename_data, (GDestroyNotify) contact_rename_data_free,
			G_OBJECT (chat));
		return;
	}

	if (actor != 0) {
		actor_contact = chat_lookup_contact (chat, actor, FALSE);
		if (actor_contact == NULL) {
			/* FIXME: handle this a tad more gracefully: perhaps
			 * the actor was a server op. We could use the
			 * contact-ids detail of MembersChangedDetailed.
			 */
			DEBUG ("actor %u not a channel member", actor);
		}
	}

	/* Remove contacts that are not members anymore */
	for (i = 0; i < removed->len; i++) {
		contact = chat_lookup_contact (chat,
			g_array_index (removed, TpHandle, i), TRUE);

		if (contact != NULL) {
			g_signal_emit_by_name (chat, "members-changed", contact,
					       actor_contact, reason, message,
					       FALSE);
			g_object_unref (contact);
		}
	}

	/* Request added contacts */
	if (added->len > 0) {
		empathy_tp_contact_factory_get_from_handles (priv->connection,
			added->len, (TpHandle *) added->data,
			tp_chat_got_added_contacts_cb, NULL, NULL,
			G_OBJECT (chat));
	}

	tp_chat_update_remote_contact (chat);

	if (actor_contact != NULL) {
		g_object_unref (actor_contact);
	}
}

static void
tp_chat_got_remote_contact_cb (TpConnection            *connection,
			       EmpathyContact          *contact,
			       const GError            *error,
			       gpointer                 user_data,
			       GObject                 *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	if (error) {
		DEBUG ("Error: %s", error->message);
		empathy_tp_chat_leave (EMPATHY_TP_CHAT (chat), "");
		return;
	}

	priv->remote_contact = g_object_ref (contact);
	g_object_notify (chat, "remote-contact");

	check_almost_ready (EMPATHY_TP_CHAT (chat));
}

static void
tp_chat_got_self_contact_cb (TpConnection            *connection,
			     EmpathyContact          *contact,
			     const GError            *error,
			     gpointer                 user_data,
			     GObject                 *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	if (error) {
		DEBUG ("Error: %s", error->message);
		empathy_tp_chat_leave (EMPATHY_TP_CHAT (chat), "");
		return;
	}

	priv->user = g_object_ref (contact);
	empathy_contact_set_is_user (priv->user, TRUE);
	check_almost_ready (EMPATHY_TP_CHAT (chat));
}

static void
password_flags_changed_cb (TpChannel *channel,
    guint added,
    guint removed,
    gpointer user_data,
    GObject *weak_object)
{
	EmpathyTpChat *self = EMPATHY_TP_CHAT (weak_object);
	EmpathyTpChatPriv *priv = GET_PRIV (self);
	gboolean was_needed, needed;

	was_needed = empathy_tp_chat_password_needed (self);

	priv->password_flags |= added;
	priv->password_flags ^= removed;

	needed = empathy_tp_chat_password_needed (self);

	if (was_needed != needed)
		g_object_notify (G_OBJECT (self), "password-needed");
}

static void
got_password_flags_cb (TpChannel *proxy,
			     guint password_flags,
			     const GError *error,
			     gpointer user_data,
			     GObject *weak_object)
{
	EmpathyTpChat *self = EMPATHY_TP_CHAT (weak_object);
	EmpathyTpChatPriv *priv = GET_PRIV (self);

	priv->got_password_flags = TRUE;
	priv->password_flags = password_flags;

	check_almost_ready (EMPATHY_TP_CHAT (self));
}

static GObject *
tp_chat_constructor (GType                  type,
		     guint                  n_props,
		     GObjectConstructParam *props)
{
	GObject           *chat;
	EmpathyTpChatPriv *priv;
	TpHandle           handle;

	chat = G_OBJECT_CLASS (empathy_tp_chat_parent_class)->constructor (type, n_props, props);

	priv = GET_PRIV (chat);

	priv->connection = g_object_ref (tp_account_get_connection (priv->account));
	tp_g_signal_connect_object (priv->channel, "invalidated",
			  G_CALLBACK (tp_chat_invalidated_cb),
			  chat, 0);

	g_assert (tp_proxy_is_prepared (priv->connection,
		TP_CONNECTION_FEATURE_CAPABILITIES));

	if (tp_proxy_has_interface_by_id (priv->channel,
					  TP_IFACE_QUARK_CHANNEL_INTERFACE_GROUP)) {
		const TpIntSet *members;
		GArray *handles;

		/* Get self contact from the group's self handle */
		handle = tp_channel_group_get_self_handle (priv->channel);
		empathy_tp_contact_factory_get_from_handle (priv->connection,
			handle, tp_chat_got_self_contact_cb,
			NULL, NULL, chat);

		/* Get initial member contacts */
		members = tp_channel_group_get_members (priv->channel);
		handles = tp_intset_to_array (members);
		empathy_tp_contact_factory_get_from_handles (priv->connection,
			handles->len, (TpHandle *) handles->data,
			tp_chat_got_added_contacts_cb, NULL, NULL, chat);

		priv->can_upgrade_to_muc = FALSE;

		tp_g_signal_connect_object (priv->channel, "group-members-changed",
			G_CALLBACK (tp_chat_group_members_changed_cb), chat, 0);
	} else {
		TpCapabilities *caps;
		GPtrArray *classes;
		guint i;

		/* Get the self contact from the connection's self handle */
		handle = tp_connection_get_self_handle (priv->connection);
		empathy_tp_contact_factory_get_from_handle (priv->connection,
			handle, tp_chat_got_self_contact_cb,
			NULL, NULL, chat);

		/* Get the remote contact */
		handle = tp_channel_get_handle (priv->channel, NULL);
		empathy_tp_contact_factory_get_from_handle (priv->connection,
			handle, tp_chat_got_remote_contact_cb,
			NULL, NULL, chat);

		caps = tp_connection_get_capabilities (priv->connection);
		g_assert (caps != NULL);

		classes = tp_capabilities_get_channel_classes (caps);

		for (i = 0; i < classes->len; i++) {
			GValueArray *array = g_ptr_array_index (classes, i);
			const char **oprops = g_value_get_boxed (
				g_value_array_get_nth (array, 1));

			if (tp_strv_contains (oprops, TP_PROP_CHANNEL_INTERFACE_CONFERENCE_INITIAL_CHANNELS)) {
				priv->can_upgrade_to_muc = TRUE;
				break;
			}
		}
	}

	if (tp_proxy_has_interface_by_id (priv->channel,
					  TP_IFACE_QUARK_PROPERTIES_INTERFACE)) {
		tp_cli_properties_interface_call_list_properties (priv->channel, -1,
								  tp_chat_list_properties_cb,
								  NULL, NULL,
								  G_OBJECT (chat));
		tp_cli_properties_interface_connect_to_properties_changed (priv->channel,
									   tp_chat_properties_changed_cb,
									   NULL, NULL,
									   G_OBJECT (chat), NULL);
		tp_cli_properties_interface_connect_to_property_flags_changed (priv->channel,
									       tp_chat_property_flags_changed_cb,
									       NULL, NULL,
									       G_OBJECT (chat), NULL);
	}

	/* Check if the chat is password protected */
	if (tp_proxy_has_interface_by_id (priv->channel,
					  TP_IFACE_QUARK_CHANNEL_INTERFACE_PASSWORD)) {
		priv->got_password_flags = FALSE;

		tp_cli_channel_interface_password_connect_to_password_flags_changed
			(priv->channel, password_flags_changed_cb, chat, NULL,
			 G_OBJECT (chat), NULL);

		tp_cli_channel_interface_password_call_get_password_flags
			(priv->channel, -1, got_password_flags_cb, chat, NULL, chat);
	} else {
		/* No Password interface, so no need to fetch the password flags */
		priv->got_password_flags = TRUE;
	}

	return chat;
}

static void
tp_chat_get_property (GObject    *object,
		      guint       param_id,
		      GValue     *value,
		      GParamSpec *pspec)
{
	EmpathyTpChat *self = EMPATHY_TP_CHAT (object);
	EmpathyTpChatPriv *priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_ACCOUNT:
		g_value_set_object (value, priv->account);
		break;
	case PROP_CHANNEL:
		g_value_set_object (value, priv->channel);
		break;
	case PROP_REMOTE_CONTACT:
		g_value_set_object (value, priv->remote_contact);
		break;
	case PROP_READY:
		g_value_set_boolean (value, priv->ready);
		break;
	case PROP_PASSWORD_NEEDED:
		g_value_set_boolean (value, empathy_tp_chat_password_needed (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
tp_chat_set_property (GObject      *object,
		      guint         param_id,
		      const GValue *value,
		      GParamSpec   *pspec)
{
	EmpathyTpChatPriv *priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_ACCOUNT:
		priv->account = g_value_dup_object (value);
		break;
	case PROP_CHANNEL:
		priv->channel = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
empathy_tp_chat_class_init (EmpathyTpChatClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = tp_chat_dispose;
	object_class->finalize = tp_chat_finalize;
	object_class->constructor = tp_chat_constructor;
	object_class->get_property = tp_chat_get_property;
	object_class->set_property = tp_chat_set_property;

	g_object_class_install_property (object_class,
					 PROP_ACCOUNT,
					 g_param_spec_object ("account",
							      "TpAccount",
							      "the account associated with the chat",
							      TP_TYPE_ACCOUNT,
							      G_PARAM_READWRITE |
							      G_PARAM_CONSTRUCT_ONLY |
							      G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (object_class,
					 PROP_CHANNEL,
					 g_param_spec_object ("channel",
							      "telepathy channel",
							      "The text channel for the chat",
							      TP_TYPE_CHANNEL,
							      G_PARAM_READWRITE |
							      G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_REMOTE_CONTACT,
					 g_param_spec_object ("remote-contact",
							      "The remote contact",
							      "The remote contact if there is no group iface on the channel",
							      EMPATHY_TYPE_CONTACT,
							      G_PARAM_READABLE));

	g_object_class_install_property (object_class,
					 PROP_READY,
					 g_param_spec_boolean ("ready",
							       "Is the object ready",
							       "This object can't be used until this becomes true",
							       FALSE,
							       G_PARAM_READABLE));

	g_object_class_install_property (object_class,
					 PROP_PASSWORD_NEEDED,
					 g_param_spec_boolean ("password-needed",
							       "password needed",
							       "TRUE if a password is needed to join the channel",
							       FALSE,
							       G_PARAM_READABLE));

	/* Signals */
	signals[MESSAGE_RECEIVED] =
		g_signal_new ("message-received",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, EMPATHY_TYPE_MESSAGE);

	signals[SEND_ERROR] =
		g_signal_new ("send-error",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      _empathy_marshal_VOID__STRING_UINT,
			      G_TYPE_NONE,
			      2, G_TYPE_STRING, G_TYPE_UINT);

	signals[CHAT_STATE_CHANGED] =
		g_signal_new ("chat-state-changed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      _empathy_marshal_VOID__OBJECT_UINT,
			      G_TYPE_NONE,
			      2, EMPATHY_TYPE_CONTACT, G_TYPE_UINT);

	signals[PROPERTY_CHANGED] =
		g_signal_new ("property-changed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      _empathy_marshal_VOID__STRING_BOXED,
			      G_TYPE_NONE,
			      2, G_TYPE_STRING, G_TYPE_VALUE);

	signals[DESTROY] =
		g_signal_new ("destroy",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	g_type_class_add_private (object_class, sizeof (EmpathyTpChatPriv));
}

static void
empathy_tp_chat_init (EmpathyTpChat *chat)
{
	EmpathyTpChatPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (chat,
		EMPATHY_TYPE_TP_CHAT, EmpathyTpChatPriv);

	chat->priv = priv;
	priv->messages_queue = g_queue_new ();
	priv->pending_messages_queue = g_queue_new ();
}

static void
tp_chat_iface_init (EmpathyContactListIface *iface)
{
	iface->add         = tp_chat_add;
	iface->remove      = tp_chat_remove;
	iface->get_members = tp_chat_get_members;
}

EmpathyTpChat *
empathy_tp_chat_new (TpAccount *account,
		     TpChannel *channel)
{
	g_return_val_if_fail (TP_IS_ACCOUNT (account), NULL);
	g_return_val_if_fail (TP_IS_TEXT_CHANNEL (channel), NULL);

	return g_object_new (EMPATHY_TYPE_TP_CHAT,
			     "account", account,
			     "channel", channel,
			     NULL);
}

const gchar *
empathy_tp_chat_get_id (EmpathyTpChat *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);
	const gchar *id;


	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (chat), NULL);

	id = tp_channel_get_identifier (priv->channel);
	if (!EMP_STR_EMPTY (id))
		return id;
	else if (priv->remote_contact)
		return empathy_contact_get_id (priv->remote_contact);
	else
		return NULL;

}

EmpathyContact *
empathy_tp_chat_get_remote_contact (EmpathyTpChat *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (chat), NULL);
	g_return_val_if_fail (priv->ready, NULL);

	return priv->remote_contact;
}

TpChannel *
empathy_tp_chat_get_channel (EmpathyTpChat *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (chat), NULL);

	return priv->channel;
}

TpAccount *
empathy_tp_chat_get_account (EmpathyTpChat *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (chat), NULL);

	return priv->account;
}

TpConnection *
empathy_tp_chat_get_connection (EmpathyTpChat *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (chat), NULL);

	return tp_channel_borrow_connection (priv->channel);
}
gboolean
empathy_tp_chat_is_ready (EmpathyTpChat *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (chat), FALSE);

	return priv->ready;
}

void
empathy_tp_chat_send (EmpathyTpChat *chat,
		      TpMessage *message)
{
	EmpathyTpChatPriv        *priv = GET_PRIV (chat);
	gchar *message_body;

	g_return_if_fail (EMPATHY_IS_TP_CHAT (chat));
	g_return_if_fail (TP_IS_CLIENT_MESSAGE (message));
	g_return_if_fail (priv->ready);

	message_body = tp_message_to_text (message, NULL);

	DEBUG ("Sending message: %s", message_body);

	tp_text_channel_send_message_async (TP_TEXT_CHANNEL (priv->channel),
		message, 0, message_send_cb, chat);

	g_free (message_body);
}

void
empathy_tp_chat_set_state (EmpathyTpChat      *chat,
			   TpChannelChatState  state)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	g_return_if_fail (EMPATHY_IS_TP_CHAT (chat));
	g_return_if_fail (priv->ready);

	if (tp_proxy_has_interface_by_id (priv->channel,
					  TP_IFACE_QUARK_CHANNEL_INTERFACE_CHAT_STATE)) {
		DEBUG ("Set state: %d", state);
		tp_cli_channel_interface_chat_state_call_set_chat_state (priv->channel, -1,
									 state,
									 tp_chat_async_cb,
									 "setting chat state",
									 NULL,
									 G_OBJECT (chat));
	}
}


const GList *
empathy_tp_chat_get_pending_messages (EmpathyTpChat *chat)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (chat), NULL);
	g_return_val_if_fail (priv->ready, NULL);

	return priv->pending_messages_queue->head;
}

static void
acknowledge_messages (EmpathyTpChat *chat, GArray *ids) {
	EmpathyTpChatPriv *priv = GET_PRIV (chat);

	tp_cli_channel_type_text_call_acknowledge_pending_messages (
		priv->channel, -1, ids, tp_chat_async_cb,
		"acknowledging received message", NULL, G_OBJECT (chat));
}

void
empathy_tp_chat_acknowledge_message (EmpathyTpChat *chat,
				     EmpathyMessage *message) {
	EmpathyTpChatPriv *priv = GET_PRIV (chat);
	GArray *message_ids;
	GList *m;
	guint id;

	g_return_if_fail (EMPATHY_IS_TP_CHAT (chat));
	g_return_if_fail (priv->ready);

	if (!empathy_message_is_incoming (message))
		goto out;

	message_ids = g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);

	id = empathy_message_get_id (message);
	g_array_append_val (message_ids, id);
	acknowledge_messages (chat, message_ids);
	g_array_free (message_ids, TRUE);

out:
	m = g_queue_find (priv->pending_messages_queue, message);
	g_assert (m != NULL);
	g_queue_delete_link (priv->pending_messages_queue, m);
	g_object_unref (message);
}

void
empathy_tp_chat_acknowledge_messages (EmpathyTpChat *chat,
				      const GSList *messages) {
	EmpathyTpChatPriv *priv = GET_PRIV (chat);
	/* Copy messages as the messges list (probably is) our own */
	GSList *msgs = g_slist_copy ((GSList *) messages);
	GSList *l;
	guint length;
	GArray *message_ids;

	g_return_if_fail (EMPATHY_IS_TP_CHAT (chat));
	g_return_if_fail (priv->ready);

	length = g_slist_length ((GSList *) messages);

	if (length == 0)
		return;

	message_ids = g_array_sized_new (FALSE, FALSE, sizeof (guint), length);

	for (l = msgs; l != NULL; l = g_slist_next (l)) {
		GList *m;

		EmpathyMessage *message = EMPATHY_MESSAGE (l->data);

		m = g_queue_find (priv->pending_messages_queue, message);
		g_assert (m != NULL);
		g_queue_delete_link (priv->pending_messages_queue, m);

		if (empathy_message_is_incoming (message)) {
			guint id = empathy_message_get_id (message);
			g_array_append_val (message_ids, id);
		}
		g_object_unref (message);
	}

	if (message_ids->len > 0)
		acknowledge_messages (chat, message_ids);

	g_array_free (message_ids, TRUE);
	g_slist_free (msgs);
}

void
empathy_tp_chat_acknowledge_all_messages (EmpathyTpChat *chat)
{
  empathy_tp_chat_acknowledge_messages (chat,
    (GSList *) empathy_tp_chat_get_pending_messages (chat));
}

gboolean
empathy_tp_chat_password_needed (EmpathyTpChat *self)
{
	EmpathyTpChatPriv *priv = GET_PRIV (self);

	return priv->password_flags & TP_CHANNEL_PASSWORD_FLAG_PROVIDE;
}

static void
provide_password_cb (TpChannel *channel,
				      gboolean correct,
				      const GError *error,
				      gpointer user_data,
				      GObject *weak_object)
{
	GSimpleAsyncResult *result = user_data;

	if (error != NULL) {
		g_simple_async_result_set_from_error (result, error);
	}
	else if (!correct) {
		/* The current D-Bus API is a bit weird so re-use the
		 * AuthenticationFailed error */
		g_simple_async_result_set_error (result, TP_ERRORS,
						 TP_ERROR_AUTHENTICATION_FAILED, "Wrong password");
	}

	g_simple_async_result_complete (result);
	g_object_unref (result);
}

void
empathy_tp_chat_provide_password_async (EmpathyTpChat *self,
						     const gchar *password,
						     GAsyncReadyCallback callback,
						     gpointer user_data)
{
	EmpathyTpChatPriv *priv = GET_PRIV (self);
	GSimpleAsyncResult *result;

	result = g_simple_async_result_new (G_OBJECT (self),
					    callback, user_data,
					    empathy_tp_chat_provide_password_finish);

	tp_cli_channel_interface_password_call_provide_password
		(priv->channel, -1, password, provide_password_cb, result,
		 NULL, G_OBJECT (self));
}

gboolean
empathy_tp_chat_provide_password_finish (EmpathyTpChat *self,
						      GAsyncResult *result,
						      GError **error)
{
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
		error))
		return FALSE;

	g_return_val_if_fail (g_simple_async_result_is_valid (result,
							      G_OBJECT (self), empathy_tp_chat_provide_password_finish), FALSE);

	return TRUE;
}

/**
 * empathy_tp_chat_can_add_contact:
 *
 * Returns: %TRUE if empathy_contact_list_add() will work for this channel.
 * That is if this chat is a 1-to-1 channel that can be upgraded to
 * a MUC using the Conference interface or if the channel is a MUC.
 */
gboolean
empathy_tp_chat_can_add_contact (EmpathyTpChat *self)
{
	EmpathyTpChatPriv *priv;

	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (self), FALSE);

	priv = GET_PRIV (self);

	return priv->can_upgrade_to_muc ||
		tp_proxy_has_interface_by_id (priv->channel,
			TP_IFACE_QUARK_CHANNEL_INTERFACE_GROUP);;
}

static void
tp_channel_leave_async_cb (GObject *source_object,
        GAsyncResult *res,
        gpointer user_data)
{
	GError *error = NULL;

	if (!tp_channel_leave_finish (TP_CHANNEL (source_object), res, &error)) {
		DEBUG ("Could not leave channel properly: (%s); closing the channel",
			error->message);
		g_error_free (error);
	}
}

void
empathy_tp_chat_leave (EmpathyTpChat *self,
		const gchar *message)
{
	EmpathyTpChatPriv *priv = GET_PRIV (self);

	tp_channel_leave_async (priv->channel, TP_CHANNEL_GROUP_CHANGE_REASON_NONE,
		message, tp_channel_leave_async_cb, self);
}

static void
add_members_cb (TpChannel *proxy,
		const GError *error,
		gpointer user_data,
		GObject *weak_object)
{
	EmpathyTpChatPriv *priv = GET_PRIV (weak_object);

	if (error != NULL) {
		DEBUG ("Failed to join chat (%s): %s",
			tp_channel_get_identifier (priv->channel), error->message);
	}
}

void
empathy_tp_chat_join (EmpathyTpChat *self)
{
	EmpathyTpChatPriv *priv = GET_PRIV (self);
	TpHandle self_handle;
	GArray *members;

	self_handle = tp_channel_group_get_self_handle (priv->channel);

	members = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 1);
	g_array_append_val (members, self_handle);

	tp_cli_channel_interface_group_call_add_members (priv->channel, -1, members,
		"", add_members_cb, NULL, NULL, G_OBJECT (self));

	g_array_free (members, TRUE);
}

gboolean
empathy_tp_chat_is_invited (EmpathyTpChat *self,
			    TpHandle *inviter)
{
	EmpathyTpChatPriv *priv = GET_PRIV (self);
	TpHandle self_handle;

	if (!tp_proxy_has_interface (priv->channel, TP_IFACE_CHANNEL_INTERFACE_GROUP))
		return FALSE;

	self_handle = tp_channel_group_get_self_handle (priv->channel);
	if (self_handle == 0)
		return FALSE;

	return tp_channel_group_get_local_pending_info (priv->channel, self_handle,
		inviter, NULL, NULL);
}

TpChannelChatState
empathy_tp_chat_get_chat_state (EmpathyTpChat *chat,
			    EmpathyContact *contact)
{
	EmpathyTpChatPriv *priv = GET_PRIV (chat);
	return tp_channel_get_chat_state (priv->channel,
		empathy_contact_get_handle (contact));
}

EmpathyContact *
empathy_tp_chat_get_self_contact (EmpathyTpChat *self)
{
	EmpathyTpChatPriv *priv = GET_PRIV (self);

	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (self), NULL);

	return priv->user;
}
