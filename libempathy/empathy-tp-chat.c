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
#include "empathy-request-util.h"
#include "empathy-time.h"
#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_TP | EMPATHY_DEBUG_CHAT
#include "empathy-debug.h"

struct _EmpathyTpChatPrivate {
	TpAccount             *account;
	EmpathyContact        *user;
	EmpathyContact        *remote_contact;
	GList                 *members;
	/* Queue of messages not signalled yet */
	GQueue                *messages_queue;
	/* Queue of messages signalled but not acked yet */
	GQueue                *pending_messages_queue;

	/* Subject */
	gboolean               supports_subject;
	gboolean               can_set_subject;
	gchar                 *subject;

	/* Room config (for now, we only track the title and don't support
	 * setting it) */
	gchar                 *title;

	gboolean               can_upgrade_to_muc;

	GHashTable            *messages_being_sent;

	/* GSimpleAsyncResult used when preparing EMPATHY_TP_CHAT_FEATURE_CORE */
	GSimpleAsyncResult    *ready_result;
};

static void tp_chat_iface_init         (EmpathyContactListIface *iface);

enum {
	PROP_0,
	PROP_ACCOUNT,
	PROP_REMOTE_CONTACT,
	PROP_N_MESSAGES_SENDING,
	PROP_TITLE,
	PROP_SUBJECT,
};

enum {
	MESSAGE_RECEIVED,
	SEND_ERROR,
	CHAT_STATE_CHANGED,
	MESSAGE_ACKNOWLEDGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE (EmpathyTpChat, empathy_tp_chat, TP_TYPE_TEXT_CHANNEL,
			 G_IMPLEMENT_INTERFACE (EMPATHY_TYPE_CONTACT_LIST,
						tp_chat_iface_init));

static void
tp_chat_set_delivery_status (EmpathyTpChat         *self,
		             const gchar           *token,
			     EmpathyDeliveryStatus  delivery_status)
{
	TpDeliveryReportingSupportFlags flags =
		tp_text_channel_get_delivery_reporting_support (
			TP_TEXT_CHANNEL (self));

	/* channel must support receiving failures and successes */
	if (!tp_str_empty (token) &&
	    flags & TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_FAILURES &&
	    flags & TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_SUCCESSES) {

		DEBUG ("Delivery status (%s) = %u", token, delivery_status);

		switch (delivery_status) {
			case EMPATHY_DELIVERY_STATUS_NONE:
				g_hash_table_remove (self->priv->messages_being_sent,
					token);
				break;

			default:
				g_hash_table_insert (self->priv->messages_being_sent,
					g_strdup (token),
					GUINT_TO_POINTER (delivery_status));
				break;
		}

		g_object_notify (G_OBJECT (self), "n-messages-sending");
	}
}

static void tp_chat_prepare_ready_async (TpProxy *proxy,
	const TpProxyFeature *feature,
	GAsyncReadyCallback callback,
	gpointer user_data);

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
	EmpathyTpChat *self = (EmpathyTpChat *) list;
	TpChannel *channel = (TpChannel *) self;

	if (tp_proxy_has_interface_by_id (self,
		TP_IFACE_QUARK_CHANNEL_INTERFACE_GROUP)) {
		TpHandle           handle;
		GArray             handles = {(gchar *) &handle, 1};

		g_return_if_fail (EMPATHY_IS_TP_CHAT (list));
		g_return_if_fail (EMPATHY_IS_CONTACT (contact));

		handle = empathy_contact_get_handle (contact);
		tp_cli_channel_interface_group_call_add_members (channel,
			-1, &handles, NULL, NULL, NULL, NULL, NULL);
	} else if (self->priv->can_upgrade_to_muc) {
		TpAccountChannelRequest *req;
		GHashTable        *props;
		const char        *object_path;
		GPtrArray          channels = { (gpointer *) &object_path, 1 };
		const char        *invitees[2] = { NULL, };

		invitees[0] = empathy_contact_get_id (contact);
		object_path = tp_proxy_get_object_path (self);

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

		req = tp_account_channel_request_new (self->priv->account, props,
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
	EmpathyTpChat *self = (EmpathyTpChat *) list;
	TpHandle           handle;
	GArray             handles = {(gchar *) &handle, 1};

	g_return_if_fail (EMPATHY_IS_TP_CHAT (list));
	g_return_if_fail (EMPATHY_IS_CONTACT (contact));

	handle = empathy_contact_get_handle (contact);
	tp_cli_channel_interface_group_call_remove_members ((TpChannel *) self, -1,
							    &handles, NULL,
							    NULL, NULL, NULL,
							    NULL);
}

static GList *
tp_chat_get_members (EmpathyContactList *list)
{
	EmpathyTpChat *self = (EmpathyTpChat *) list;
	GList             *members = NULL;

	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (list), NULL);

	if (self->priv->members) {
		members = g_list_copy (self->priv->members);
		g_list_foreach (members, (GFunc) g_object_ref, NULL);
	} else {
		members = g_list_prepend (members, g_object_ref (self->priv->user));
		if (self->priv->remote_contact != NULL)
			members = g_list_prepend (members, g_object_ref (self->priv->remote_contact));
	}

	return members;
}

static void
check_ready (EmpathyTpChat *self)
{
	if (self->priv->ready_result == NULL)
		return;

	if (g_queue_get_length (self->priv->messages_queue) > 0)
		return;

	DEBUG ("Ready");

	g_simple_async_result_complete (self->priv->ready_result);
	tp_clear_object (&self->priv->ready_result);
}

static void
tp_chat_emit_queued_messages (EmpathyTpChat *self)
{
	EmpathyMessage    *message;

	/* Check if we can now emit some queued messages */
	while ((message = g_queue_peek_head (self->priv->messages_queue)) != NULL) {
		if (empathy_message_get_sender (message) == NULL) {
			break;
		}

		DEBUG ("Queued message ready");
		g_queue_pop_head (self->priv->messages_queue);
		g_queue_push_tail (self->priv->pending_messages_queue, message);
		g_signal_emit (self, signals[MESSAGE_RECEIVED], 0, message);
	}

	check_ready (self);
}

static void
tp_chat_got_sender_cb (TpConnection            *connection,
		       EmpathyContact          *contact,
		       const GError            *error,
		       gpointer                 message,
		       GObject                 *chat)
{
	EmpathyTpChat *self = (EmpathyTpChat *) chat;

	if (error) {
		DEBUG ("Error: %s", error->message);
		/* Do not block the message queue, just drop this message */
		g_queue_remove (self->priv->messages_queue, message);
	} else {
		empathy_message_set_sender (message, contact);
	}

	tp_chat_emit_queued_messages (EMPATHY_TP_CHAT (self));
}

static void
tp_chat_build_message (EmpathyTpChat *self,
		       TpMessage     *msg,
		       gboolean       incoming)
{
	EmpathyMessage    *message;
	TpContact *sender;

	message = empathy_message_new_from_tp_message (msg, incoming);
	/* FIXME: this is actually a lie for incoming messages. */
	empathy_message_set_receiver (message, self->priv->user);

	g_queue_push_tail (self->priv->messages_queue, message);

	sender = tp_signalled_message_get_sender (msg);
	g_assert (sender != NULL);

	if (tp_contact_get_handle (sender) == 0) {
		empathy_message_set_sender (message, self->priv->user);
		tp_chat_emit_queued_messages (self);
	} else {
		TpConnection *connection = tp_channel_borrow_connection (
			(TpChannel *) self);

		empathy_tp_contact_factory_get_from_handle (connection,
			tp_contact_get_handle (sender),
			tp_chat_got_sender_cb,
			message, NULL, G_OBJECT (self));
	}
}

static void
handle_delivery_report (EmpathyTpChat *self,
		TpMessage *message)
{
	TpDeliveryStatus delivery_status;
	const GHashTable *header;
	TpChannelTextSendError delivery_error;
	gboolean valid;
	GPtrArray *echo;
	const gchar *message_body = NULL;
	const gchar *delivery_dbus_error;
	const gchar *delivery_token = NULL;

	header = tp_message_peek (message, 0);
	if (header == NULL)
		goto out;

	delivery_token = tp_asv_get_string (header, "delivery-token");
	delivery_status = tp_asv_get_uint32 (header, "delivery-status", &valid);

	if (!valid) {
		goto out;
	} else if (delivery_status == TP_DELIVERY_STATUS_ACCEPTED) {
		DEBUG ("Accepted %s", delivery_token);
		tp_chat_set_delivery_status (self, delivery_token,
			EMPATHY_DELIVERY_STATUS_ACCEPTED);
		goto out;
	} else if (delivery_status == TP_DELIVERY_STATUS_DELIVERED) {
		DEBUG ("Delivered %s", delivery_token);
		tp_chat_set_delivery_status (self, delivery_token,
			EMPATHY_DELIVERY_STATUS_NONE);
		goto out;
	} else if (delivery_status != TP_DELIVERY_STATUS_PERMANENTLY_FAILED &&
		   delivery_status != TP_DELIVERY_STATUS_TEMPORARILY_FAILED) {
		goto out;
	}

	delivery_error = tp_asv_get_uint32 (header, "delivery-error", &valid);
	if (!valid)
		delivery_error = TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN;

	delivery_dbus_error = tp_asv_get_string (header, "delivery-dbus-error");

	/* TODO: ideally we should use tp-glib API giving us the echoed message as a
	 * TpMessage. (fdo #35884) */
	echo = tp_asv_get_boxed (header, "delivery-echo",
		TP_ARRAY_TYPE_MESSAGE_PART_LIST);
	if (echo != NULL && echo->len >= 2) {
		const GHashTable *echo_body;

		echo_body = g_ptr_array_index (echo, 1);
		if (echo_body != NULL)
			message_body = tp_asv_get_string (echo_body, "content");
	}

	tp_chat_set_delivery_status (self, delivery_token,
			EMPATHY_DELIVERY_STATUS_NONE);
	g_signal_emit (self, signals[SEND_ERROR], 0, message_body,
			delivery_error, delivery_dbus_error);

out:
	tp_text_channel_ack_message_async (TP_TEXT_CHANNEL (self),
		message, NULL, NULL);
}

static void
handle_incoming_message (EmpathyTpChat *self,
			 TpMessage *message,
			 gboolean pending)
{
	gchar *message_body;

	if (tp_message_is_delivery_report (message)) {
		handle_delivery_report (self, message);
		return;
	}

	message_body = tp_message_to_text (message, NULL);

	DEBUG ("Message %s (channel %s): %s",
		pending ? "pending" : "received",
		tp_proxy_get_object_path (self), message_body);

	if (message_body == NULL) {
		DEBUG ("Empty message with NonTextContent, ignoring and acking.");

		tp_text_channel_ack_message_async (TP_TEXT_CHANNEL (self),
			message, NULL, NULL);
		return;
	}

	tp_chat_build_message (self, message, TRUE);

	g_free (message_body);
}

static void
message_received_cb (TpTextChannel   *channel,
		     TpMessage *message,
		     EmpathyTpChat *self)
{
	handle_incoming_message (self, message, FALSE);
}

static gboolean
find_pending_message_func (gconstpointer a,
			   gconstpointer b)
{
	EmpathyMessage *msg = (EmpathyMessage *) a;
	TpMessage *message = (TpMessage *) b;

	if (empathy_message_get_tp_message (msg) == message)
		return 0;

	return -1;
}

static void
pending_message_removed_cb (TpTextChannel   *channel,
		            TpMessage *message,
		            EmpathyTpChat *self)
{
	GList *m;

	m = g_queue_find_custom (self->priv->pending_messages_queue, message,
				 find_pending_message_func);

	if (m == NULL)
		return;

	g_signal_emit (self, signals[MESSAGE_ACKNOWLEDGED], 0, m->data);

	g_object_unref (m->data);
	g_queue_delete_link (self->priv->pending_messages_queue, m);
}

static void
message_sent_cb (TpTextChannel   *channel,
		 TpMessage *message,
		 TpMessageSendingFlags flags,
		 gchar              *token,
		 EmpathyTpChat      *self)
{
	gchar *message_body;

	message_body = tp_message_to_text (message, NULL);

	DEBUG ("Message sent: %s", message_body);

	tp_chat_build_message (self, message, FALSE);

	g_free (message_body);
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
	EmpathyTpChat *self = user_data;
	TpTextChannel *channel = (TpTextChannel *) source;
	gchar *token = NULL;
	GError *error = NULL;

	if (!tp_text_channel_send_message_finish (channel, result, &token, &error)) {
		DEBUG ("Error: %s", error->message);

		/* FIXME: we should use the body of the message as first argument of the
		 * signal but can't easily get it as we just get a user_data pointer. Once
		 * we'll have rebased EmpathyTpChat on top of TpTextChannel we'll be able
		 * to use the user_data pointer to pass the message and fix this. */
		g_signal_emit (self, signals[SEND_ERROR], 0,
			       NULL, error_to_text_send_error (error), NULL);

		g_error_free (error);
	}

	tp_chat_set_delivery_status (self, token,
		EMPATHY_DELIVERY_STATUS_SENDING);
	g_free (token);
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
			  EmpathyTpChat *self)
{
	TpConnection *connection = tp_channel_borrow_connection (
		(TpChannel *) self);

	empathy_tp_contact_factory_get_from_handle (connection, handle,
		tp_chat_state_changed_got_contact_cb, GUINT_TO_POINTER (state),
		NULL, G_OBJECT (self));
}

static void
list_pending_messages (EmpathyTpChat *self)
{
	GList *messages, *l;

	messages = tp_text_channel_get_pending_messages (
		TP_TEXT_CHANNEL (self));

	for (l = messages; l != NULL; l = g_list_next (l)) {
		TpMessage *message = l->data;

		handle_incoming_message (self, message, FALSE);
	}

	g_list_free (messages);
}

static void
update_subject (EmpathyTpChat *self,
		GHashTable *properties)
{
	EmpathyTpChatPrivate *priv = self->priv;
	gboolean can_set, valid;
	const gchar *subject;

	can_set = tp_asv_get_boolean (properties, "CanSet", &valid);
	if (valid) {
		priv->can_set_subject = can_set;
	}

	subject = tp_asv_get_string (properties, "Subject");
	if (subject != NULL) {
		g_free (priv->subject);
		priv->subject = g_strdup (subject);
		g_object_notify (G_OBJECT (self), "subject");
	}

	/* TODO: track Actor and Timestamp. */
}

static void
tp_chat_get_all_subject_cb (TpProxy      *proxy,
			    GHashTable   *properties,
			    const GError *error,
			    gpointer      user_data G_GNUC_UNUSED,
			    GObject      *chat)
{
	EmpathyTpChat *self = EMPATHY_TP_CHAT (chat);
	EmpathyTpChatPrivate *priv = self->priv;

	if (error) {
		DEBUG ("Error fetching subject: %s", error->message);
		return;
	}

	priv->supports_subject = TRUE;
	update_subject (self, properties);
}

static void
update_title (EmpathyTpChat *self,
	      GHashTable *properties)
{
	EmpathyTpChatPrivate *priv = self->priv;
	const gchar *title = tp_asv_get_string (properties, "Title");

	if (title != NULL) {
		if (tp_str_empty (title)) {
			title = NULL;
		}

		g_free (priv->title);
		priv->title = g_strdup (title);
		g_object_notify (G_OBJECT (self), "title");
	}
}

static void
tp_chat_get_all_room_config_cb (TpProxy      *proxy,
				GHashTable   *properties,
				const GError *error,
				gpointer      user_data G_GNUC_UNUSED,
				GObject      *chat)
{
	EmpathyTpChat *self = EMPATHY_TP_CHAT (chat);

	if (error) {
		DEBUG ("Error fetching room config: %s", error->message);
		return;
	}

	update_title (self, properties);
}

static void
tp_chat_dbus_properties_changed_cb (TpProxy *proxy,
				    const gchar *interface_name,
				    GHashTable *changed,
				    const gchar **invalidated,
				    gpointer user_data,
				    GObject *chat)
{
	EmpathyTpChat *self = EMPATHY_TP_CHAT (chat);

	if (!tp_strdiff (interface_name, TP_IFACE_CHANNEL_INTERFACE_SUBJECT)) {
		update_subject (self, changed);
	}

	if (!tp_strdiff (interface_name, TP_IFACE_CHANNEL_INTERFACE_ROOM_CONFIG)) {
		update_title (self, changed);
	}
}

void
empathy_tp_chat_set_subject (EmpathyTpChat *self,
			     const gchar   *subject)
{
	tp_cli_channel_interface_subject_call_set_subject (TP_CHANNEL (self), -1,
							   subject,
							   tp_chat_async_cb,
							   "while setting subject", NULL,
							   G_OBJECT (self));
}

const gchar *
empathy_tp_chat_get_title (EmpathyTpChat *self)
{
	EmpathyTpChatPrivate *priv = self->priv;

	return priv->title;
}

gboolean
empathy_tp_chat_supports_subject (EmpathyTpChat *self)
{
	EmpathyTpChatPrivate *priv = self->priv;

	return priv->supports_subject;
}

gboolean
empathy_tp_chat_can_set_subject (EmpathyTpChat *self)
{
	EmpathyTpChatPrivate *priv = self->priv;

	return priv->can_set_subject;
}

const gchar *
empathy_tp_chat_get_subject (EmpathyTpChat *self)
{
	EmpathyTpChatPrivate *priv = self->priv;

	return priv->subject;
}

static void
tp_chat_dispose (GObject *object)
{
	EmpathyTpChat *self = EMPATHY_TP_CHAT (object);

	tp_clear_object (&self->priv->account);
	tp_clear_object (&self->priv->remote_contact);
	tp_clear_object (&self->priv->user);

	g_queue_foreach (self->priv->messages_queue, (GFunc) g_object_unref, NULL);
	g_queue_clear (self->priv->messages_queue);

	g_queue_foreach (self->priv->pending_messages_queue,
		(GFunc) g_object_unref, NULL);
	g_queue_clear (self->priv->pending_messages_queue);

	tp_clear_object (&self->priv->ready_result);

	if (G_OBJECT_CLASS (empathy_tp_chat_parent_class)->dispose)
		G_OBJECT_CLASS (empathy_tp_chat_parent_class)->dispose (object);
}

static void
tp_chat_finalize (GObject *object)
{
	EmpathyTpChat *self = (EmpathyTpChat *) object;

	DEBUG ("Finalize: %p", object);

	g_queue_free (self->priv->messages_queue);
	g_queue_free (self->priv->pending_messages_queue);
	g_hash_table_unref (self->priv->messages_being_sent);

	g_free (self->priv->title);
	g_free (self->priv->subject);

	G_OBJECT_CLASS (empathy_tp_chat_parent_class)->finalize (object);
}

static void
check_almost_ready (EmpathyTpChat *self)
{
	TpChannel *channel = (TpChannel *) self;

	if (self->priv->ready_result == NULL)
		return;

	if (self->priv->user == NULL)
		return;

	/* We need either the members (room) or the remote contact (private chat).
	 * If the chat is protected by a password we can't get these information so
	 * consider the chat as ready so it can be presented to the user. */
	if (!tp_channel_password_needed (channel) && self->priv->members == NULL &&
	    self->priv->remote_contact == NULL)
		return;

	g_assert (tp_proxy_is_prepared (self,
		TP_TEXT_CHANNEL_FEATURE_INCOMING_MESSAGES));

	tp_g_signal_connect_object (self, "message-received",
		G_CALLBACK (message_received_cb), self, 0);
	tp_g_signal_connect_object (self, "pending-message-removed",
		G_CALLBACK (pending_message_removed_cb), self, 0);

	list_pending_messages (self);

	tp_g_signal_connect_object (self, "message-sent",
		G_CALLBACK (message_sent_cb), self, 0);

	tp_g_signal_connect_object (self, "chat-state-changed",
		G_CALLBACK (tp_chat_state_changed_cb), self, 0);

	check_ready (self);
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
	EmpathyTpChat *self = (EmpathyTpChat *) chat;
	guint i;
	const TpIntSet *members;
	TpHandle handle;
	EmpathyContact *contact;

	if (error) {
		DEBUG ("Error: %s", error->message);
		return;
	}

	members = tp_channel_group_get_members ((TpChannel *) self);
	for (i = 0; i < n_contacts; i++) {
		contact = contacts[i];
		handle = empathy_contact_get_handle (contact);

		/* Make sure the contact is still member */
		if (tp_intset_is_member (members, handle)) {
			self->priv->members = g_list_prepend (self->priv->members,
				g_object_ref (contact));
			g_signal_emit_by_name (chat, "members-changed",
					       contact, NULL, 0, NULL, TRUE);
		}
	}

	check_almost_ready (EMPATHY_TP_CHAT (chat));
}

static EmpathyContact *
chat_lookup_contact (EmpathyTpChat *self,
		     TpHandle       handle,
		     gboolean       remove_)
{
	GList *l;

	for (l = self->priv->members; l; l = l->next) {
		EmpathyContact *c = l->data;

		if (empathy_contact_get_handle (c) != handle) {
			continue;
		}

		if (remove_) {
			/* Caller takes the reference. */
			self->priv->members = g_list_delete_link (self->priv->members, l);
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
	EmpathyTpChat *self = (EmpathyTpChat *) chat;
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

	members = tp_channel_group_get_members ((TpChannel *) self);
	handle = empathy_contact_get_handle (new);

	old = chat_lookup_contact (self, rename_data->old_handle, TRUE);

	/* Make sure the contact is still member */
	if (tp_intset_is_member (members, handle)) {
		self->priv->members = g_list_prepend (self->priv->members,
			g_object_ref (new));

		if (old != NULL) {
			g_signal_emit_by_name (self, "member-renamed",
					       old, new, rename_data->reason,
					       rename_data->message);
			g_object_unref (old);
		}
	}

	if (self->priv->user == old) {
		/* We change our nick */
		tp_clear_object (&self->priv->user);
		self->priv->user = g_object_ref (new);
	}

	check_almost_ready (self);
}


static void
tp_chat_group_members_changed_cb (TpChannel     *channel,
				  gchar         *message,
				  GArray        *added,
				  GArray        *removed,
				  GArray        *local_pending,
				  GArray        *remote_pending,
				  guint          actor,
				  guint          reason,
				  EmpathyTpChat *self)
{
	EmpathyContact *contact;
	EmpathyContact *actor_contact = NULL;
	guint i;
	ContactRenameData *rename_data;
	TpHandle old_handle;
	TpConnection *connection = tp_channel_borrow_connection (
		(TpChannel *) self);

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
		empathy_tp_contact_factory_get_from_handles (connection,
			added->len, (TpHandle *) added->data,
			tp_chat_got_renamed_contacts_cb,
			rename_data, (GDestroyNotify) contact_rename_data_free,
			G_OBJECT (self));
		return;
	}

	if (actor != 0) {
		actor_contact = chat_lookup_contact (self, actor, FALSE);
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
		contact = chat_lookup_contact (self,
			g_array_index (removed, TpHandle, i), TRUE);

		if (contact != NULL) {
			g_signal_emit_by_name (self, "members-changed", contact,
					       actor_contact, reason, message,
					       FALSE);
			g_object_unref (contact);
		}
	}

	/* Request added contacts */
	if (added->len > 0) {
		empathy_tp_contact_factory_get_from_handles (connection,
			added->len, (TpHandle *) added->data,
			tp_chat_got_added_contacts_cb, NULL, NULL,
			G_OBJECT (self));
	}

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
	EmpathyTpChat *self = (EmpathyTpChat *) chat;

	if (error) {
		DEBUG ("Error: %s", error->message);
		empathy_tp_chat_leave (self, "");
		return;
	}

	self->priv->remote_contact = g_object_ref (contact);
	g_object_notify (chat, "remote-contact");

	check_almost_ready (self);
}

static void
tp_chat_got_self_contact_cb (TpConnection            *connection,
			     EmpathyContact          *contact,
			     const GError            *error,
			     gpointer                 user_data,
			     GObject                 *chat)
{
	EmpathyTpChat *self = (EmpathyTpChat *) chat;

	if (error) {
		DEBUG ("Error: %s", error->message);
		empathy_tp_chat_leave (self, "");
		return;
	}

	self->priv->user = g_object_ref (contact);
	empathy_contact_set_is_user (self->priv->user, TRUE);
	check_almost_ready (self);
}

static void
tp_chat_get_property (GObject    *object,
		      guint       param_id,
		      GValue     *value,
		      GParamSpec *pspec)
{
	EmpathyTpChat *self = EMPATHY_TP_CHAT (object);

	switch (param_id) {
	case PROP_ACCOUNT:
		g_value_set_object (value, self->priv->account);
		break;
	case PROP_REMOTE_CONTACT:
		g_value_set_object (value, self->priv->remote_contact);
		break;
	case PROP_N_MESSAGES_SENDING:
		g_value_set_uint (value,
			g_hash_table_size (self->priv->messages_being_sent));
		break;
	case PROP_TITLE:
		g_value_set_string (value,
			empathy_tp_chat_get_title (self));
		break;
	case PROP_SUBJECT:
		g_value_set_string (value,
			empathy_tp_chat_get_subject (self));
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
	EmpathyTpChat *self = EMPATHY_TP_CHAT (object);

	switch (param_id) {
	case PROP_ACCOUNT:
		self->priv->account = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

enum {
	FEAT_READY,
	N_FEAT
};

static const TpProxyFeature *
tp_chat_list_features (TpProxyClass *cls G_GNUC_UNUSED)
{
	static TpProxyFeature features[N_FEAT + 1] = { { 0 } };
  static GQuark need[2] = {0, 0};

	if (G_LIKELY (features[0].name != 0))
		return features;

	features[FEAT_READY].name = EMPATHY_TP_CHAT_FEATURE_READY;
	need[0] = TP_TEXT_CHANNEL_FEATURE_INCOMING_MESSAGES;
	features[FEAT_READY].depends_on = need;
	features[FEAT_READY].prepare_async =
		tp_chat_prepare_ready_async;

	/* assert that the terminator at the end is there */
	g_assert (features[N_FEAT].name == 0);

	return features;
}

static void
empathy_tp_chat_class_init (EmpathyTpChatClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	TpProxyClass *proxy_class = TP_PROXY_CLASS (klass);

	object_class->dispose = tp_chat_dispose;
	object_class->finalize = tp_chat_finalize;
	object_class->get_property = tp_chat_get_property;
	object_class->set_property = tp_chat_set_property;

	proxy_class->list_features = tp_chat_list_features;

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
					 PROP_REMOTE_CONTACT,
					 g_param_spec_object ("remote-contact",
							      "The remote contact",
							      "The remote contact if there is no group iface on the channel",
							      EMPATHY_TYPE_CONTACT,
							      G_PARAM_READABLE));

	g_object_class_install_property (object_class,
					 PROP_N_MESSAGES_SENDING,
					 g_param_spec_uint ("n-messages-sending",
						 	    "Num Messages Sending",
							    "The number of messages being sent",
							    0, G_MAXUINT, 0,
							    G_PARAM_READABLE));

	g_object_class_install_property (object_class,
					 PROP_TITLE,
					 g_param_spec_string ("title",
							      "Title",
							      "A human-readable name for the room, if any",
							      NULL,
							      G_PARAM_READABLE |
							      G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (object_class,
					 PROP_SUBJECT,
					 g_param_spec_string ("subject",
							      "Subject",
							      "The room's current subject, if any",
							      NULL,
							      G_PARAM_READABLE |
							      G_PARAM_STATIC_STRINGS));

	/* Signals */
	signals[MESSAGE_RECEIVED] =
		g_signal_new ("message-received-empathy",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_generic,
			      G_TYPE_NONE,
			      1, EMPATHY_TYPE_MESSAGE);

	signals[SEND_ERROR] =
		g_signal_new ("send-error",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_generic,
			      G_TYPE_NONE,
			      3, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING);

	signals[CHAT_STATE_CHANGED] =
		g_signal_new ("chat-state-changed-empathy",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_generic,
			      G_TYPE_NONE,
			      2, EMPATHY_TYPE_CONTACT, G_TYPE_UINT);

	signals[MESSAGE_ACKNOWLEDGED] =
		g_signal_new ("message-acknowledged",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_generic,
			      G_TYPE_NONE,
			      1, EMPATHY_TYPE_MESSAGE);

	g_type_class_add_private (object_class, sizeof (EmpathyTpChatPrivate));
}

static void
empathy_tp_chat_init (EmpathyTpChat *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
		EMPATHY_TYPE_TP_CHAT, EmpathyTpChatPrivate);

	self->priv->messages_queue = g_queue_new ();
	self->priv->pending_messages_queue = g_queue_new ();
	self->priv->messages_being_sent = g_hash_table_new_full (
		g_str_hash, g_str_equal, g_free, NULL);
}

static void
tp_chat_iface_init (EmpathyContactListIface *iface)
{
	iface->add         = tp_chat_add;
	iface->remove      = tp_chat_remove;
	iface->get_members = tp_chat_get_members;
}

EmpathyTpChat *
empathy_tp_chat_new (
		     TpSimpleClientFactory *factory,
		     TpAccount *account,
		     TpConnection *conn,
		     const gchar *object_path,
		     const GHashTable *immutable_properties)
{
	TpProxy *conn_proxy = (TpProxy *) conn;

	g_return_val_if_fail (TP_IS_ACCOUNT (account), NULL);
	g_return_val_if_fail (TP_IS_CONNECTION (conn), NULL);
	g_return_val_if_fail (immutable_properties != NULL, NULL);

	return g_object_new (EMPATHY_TYPE_TP_CHAT,
			     "factory", factory,
			     "account", account,
			     "connection", conn,
			     "dbus-daemon", conn_proxy->dbus_daemon,
			     "bus-name", conn_proxy->bus_name,
			     "object-path", object_path,
			     "channel-properties", immutable_properties,
			     NULL);
}

const gchar *
empathy_tp_chat_get_id (EmpathyTpChat *self)
{
	const gchar *id;

	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (self), NULL);

	id = tp_channel_get_identifier ((TpChannel *) self);
	if (!EMP_STR_EMPTY (id))
		return id;
	else if (self->priv->remote_contact)
		return empathy_contact_get_id (self->priv->remote_contact);
	else
		return NULL;

}

EmpathyContact *
empathy_tp_chat_get_remote_contact (EmpathyTpChat *self)
{
	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (self), NULL);

	return self->priv->remote_contact;
}

TpAccount *
empathy_tp_chat_get_account (EmpathyTpChat *self)
{
	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (self), NULL);

	return self->priv->account;
}

void
empathy_tp_chat_send (EmpathyTpChat *self,
		      TpMessage *message)
{
	gchar *message_body;

	g_return_if_fail (EMPATHY_IS_TP_CHAT (self));
	g_return_if_fail (TP_IS_CLIENT_MESSAGE (message));

	message_body = tp_message_to_text (message, NULL);

	DEBUG ("Sending message: %s", message_body);

	tp_text_channel_send_message_async (TP_TEXT_CHANNEL (self),
		message, TP_MESSAGE_SENDING_FLAG_REPORT_DELIVERY,
		message_send_cb, self);

	g_free (message_body);
}

const GList *
empathy_tp_chat_get_pending_messages (EmpathyTpChat *self)
{
	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (self), NULL);

	return self->priv->pending_messages_queue->head;
}

void
empathy_tp_chat_acknowledge_message (EmpathyTpChat *self,
				     EmpathyMessage *message) {
	TpMessage *tp_msg;

	g_return_if_fail (EMPATHY_IS_TP_CHAT (self));

	if (!empathy_message_is_incoming (message))
		return;

	tp_msg = empathy_message_get_tp_message (message);
	tp_text_channel_ack_message_async (TP_TEXT_CHANNEL (self),
					   tp_msg, NULL, NULL);
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
	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (self), FALSE);

	return self->priv->can_upgrade_to_muc ||
		tp_proxy_has_interface_by_id (self,
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
	TpChannel *channel = (TpChannel *) self;

	DEBUG ("Leaving channel %s with message \"%s\"",
		tp_channel_get_identifier (channel), message);

	tp_channel_leave_async (channel, TP_CHANNEL_GROUP_CHANGE_REASON_NONE,
		message, tp_channel_leave_async_cb, self);
}

static void
add_members_cb (TpChannel *proxy,
		const GError *error,
		gpointer user_data,
		GObject *weak_object)
{
	EmpathyTpChat *self = (EmpathyTpChat *) weak_object;

	if (error != NULL) {
		DEBUG ("Failed to join chat (%s): %s",
			tp_channel_get_identifier ((TpChannel *) self), error->message);
	}
}

void
empathy_tp_chat_join (EmpathyTpChat *self)
{
	TpHandle self_handle;
	GArray *members;

	self_handle = tp_channel_group_get_self_handle ((TpChannel *) self);

	members = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 1);
	g_array_append_val (members, self_handle);

	tp_cli_channel_interface_group_call_add_members ((TpChannel *) self, -1, members,
		"", add_members_cb, NULL, NULL, G_OBJECT (self));

	g_array_unref (members);
}

gboolean
empathy_tp_chat_is_invited (EmpathyTpChat *self,
			    TpHandle *inviter)
{
	TpHandle self_handle;

	if (!tp_proxy_has_interface (self, TP_IFACE_CHANNEL_INTERFACE_GROUP))
		return FALSE;

	self_handle = tp_channel_group_get_self_handle ((TpChannel *) self);
	if (self_handle == 0)
		return FALSE;

	return tp_channel_group_get_local_pending_info ((TpChannel *) self, self_handle,
		inviter, NULL, NULL);
}

TpChannelChatState
empathy_tp_chat_get_chat_state (EmpathyTpChat *self,
			    EmpathyContact *contact)
{
	return tp_channel_get_chat_state ((TpChannel *) self,
		empathy_contact_get_handle (contact));
}

EmpathyContact *
empathy_tp_chat_get_self_contact (EmpathyTpChat *self)
{
	g_return_val_if_fail (EMPATHY_IS_TP_CHAT (self), NULL);

	return self->priv->user;
}

GQuark
empathy_tp_chat_get_feature_ready (void)
{
	return g_quark_from_static_string ("empathy-tp-chat-feature-ready");
}

static void
tp_chat_prepare_ready_async (TpProxy *proxy,
	const TpProxyFeature *feature,
	GAsyncReadyCallback callback,
	gpointer user_data)
{
	EmpathyTpChat *self = (EmpathyTpChat *) proxy;
	TpChannel *channel = (TpChannel *) proxy;
	TpConnection *connection;
	gboolean listen_for_dbus_properties_changed = FALSE;

	g_assert (self->priv->ready_result == NULL);
	self->priv->ready_result = g_simple_async_result_new (G_OBJECT (self),
		callback, user_data, tp_chat_prepare_ready_async);

	connection = tp_channel_borrow_connection (channel);

	if (tp_proxy_has_interface_by_id (self,
					  TP_IFACE_QUARK_CHANNEL_INTERFACE_GROUP)) {
		const TpIntSet *members;
		GArray *handles;
		TpHandle handle;

		/* Get self contact from the group's self handle */
		handle = tp_channel_group_get_self_handle (channel);
		empathy_tp_contact_factory_get_from_handle (connection,
			handle, tp_chat_got_self_contact_cb,
			NULL, NULL, G_OBJECT (self));

		/* Get initial member contacts */
		members = tp_channel_group_get_members (channel);
		handles = tp_intset_to_array (members);
		empathy_tp_contact_factory_get_from_handles (connection,
			handles->len, (TpHandle *) handles->data,
			tp_chat_got_added_contacts_cb, NULL, NULL, G_OBJECT (self));

		self->priv->can_upgrade_to_muc = FALSE;

		tp_g_signal_connect_object (self, "group-members-changed",
			G_CALLBACK (tp_chat_group_members_changed_cb), self, 0);
	} else {
		TpCapabilities *caps;
		GPtrArray *classes;
		guint i;
		TpHandle handle;

		/* Get the self contact from the connection's self handle */
		handle = tp_connection_get_self_handle (connection);
		empathy_tp_contact_factory_get_from_handle (connection,
			handle, tp_chat_got_self_contact_cb,
			NULL, NULL, G_OBJECT (self));

		/* Get the remote contact */
		handle = tp_channel_get_handle (channel, NULL);
		empathy_tp_contact_factory_get_from_handle (connection,
			handle, tp_chat_got_remote_contact_cb,
			NULL, NULL, G_OBJECT (self));

		caps = tp_connection_get_capabilities (connection);
		g_assert (caps != NULL);

		classes = tp_capabilities_get_channel_classes (caps);

		for (i = 0; i < classes->len; i++) {
			GValueArray *array = g_ptr_array_index (classes, i);
			const char **oprops = g_value_get_boxed (
				g_value_array_get_nth (array, 1));

			if (tp_strv_contains (oprops, TP_PROP_CHANNEL_INTERFACE_CONFERENCE_INITIAL_CHANNELS)) {
				self->priv->can_upgrade_to_muc = TRUE;
				break;
			}
		}
	}

	if (tp_proxy_has_interface_by_id (self,
					  TP_IFACE_QUARK_CHANNEL_INTERFACE_SUBJECT)) {
		tp_cli_dbus_properties_call_get_all (channel, -1,
						     TP_IFACE_CHANNEL_INTERFACE_SUBJECT,
						     tp_chat_get_all_subject_cb,
						     NULL, NULL,
						     G_OBJECT (self));
		listen_for_dbus_properties_changed = TRUE;
	}

	if (tp_proxy_has_interface_by_id (self,
					  TP_IFACE_QUARK_CHANNEL_INTERFACE_ROOM_CONFIG)) {
		tp_cli_dbus_properties_call_get_all (channel, -1,
						     TP_IFACE_CHANNEL_INTERFACE_ROOM_CONFIG,
						     tp_chat_get_all_room_config_cb,
						     NULL, NULL,
						     G_OBJECT (self));
		listen_for_dbus_properties_changed = TRUE;
	}

	if (listen_for_dbus_properties_changed) {
		tp_cli_dbus_properties_connect_to_properties_changed (channel,
								      tp_chat_dbus_properties_changed_cb,
								      NULL, NULL,
								      G_OBJECT (self), NULL);
	}
}
