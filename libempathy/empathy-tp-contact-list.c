/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007 Xavier Claessens <xclaesse@gmail.com>
 * Copyright (C) 2007-2009 Collabora Ltd.
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
#include <glib/gi18n-lib.h>

#include <telepathy-glib/channel.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>

#include <extensions/extensions.h>

#include "empathy-tp-contact-list.h"
#include "empathy-tp-contact-factory.h"
#include "empathy-contact-list.h"
#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_TP | EMPATHY_DEBUG_CONTACT
#include "empathy-debug.h"

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyTpContactList)
typedef struct {
	TpConnection   *connection;

	TpChannel      *publish;
	TpChannel      *subscribe;
	TpChannel      *stored;
	/* contact handle (TpHandle) => reffed (EmpathyContact *)
	 *
	 * Union of:
	 *  - members of 'subscribe': we receive their presence
	 *  - RP of 'subscribe': we asked to receive their presence
	 *  - members of 'publish': we send them our presence
	 *  - members of 'stored': they are in our roster
	 */
	GHashTable     *members;
	/* contact handle (TpHandle) => reffed (EmpathyContact *)
	 *
	 * Contacts which are local-pending in the publish channel but are NOT in
	 * the members hash table: they asked to receive our presence and we don't
	 * receive theirs or asked to.
	 * That's basically the contacts which asked to add us to their contact
	 * list and we didn't answer it. */
	GHashTable     *pendings;
	/* group name: borrowed (const gchar *)  => reffed (TpChannel *) */
	GHashTable     *groups;
	/* group name: owned (gchar *) => owned GArray of TpHandle */
	GHashTable     *add_to_group;

	EmpathyContactListFlags flags;
} EmpathyTpContactListPriv;

typedef enum {
	TP_CONTACT_LIST_TYPE_PUBLISH,
	TP_CONTACT_LIST_TYPE_SUBSCRIBE,
	TP_CONTACT_LIST_TYPE_UNKNOWN
} TpContactListType;

static void tp_contact_list_iface_init         (EmpathyContactListIface   *iface);

enum {
	PROP_0,
	PROP_CONNECTION,
};

G_DEFINE_TYPE_WITH_CODE (EmpathyTpContactList, empathy_tp_contact_list, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (EMPATHY_TYPE_CONTACT_LIST,
						tp_contact_list_iface_init));

static void
tp_contact_list_forget_group (EmpathyTpContactList *list,
			      TpChannel *channel)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	const TpIntSet *members;
	TpIntSetIter iter;
	const gchar *group_name;

	group_name = tp_channel_get_identifier (channel);

	/* Signal that all members are not in that group anymore */
	members = tp_channel_group_get_members (channel);
	tp_intset_iter_init (&iter, members);
	while (tp_intset_iter_next (&iter)) {
		EmpathyContact *contact;

		contact = g_hash_table_lookup (priv->members,
					       GUINT_TO_POINTER (iter.element));
		if (contact == NULL) {
			continue;
		}

		DEBUG ("Contact %s (%d) removed from group %s",
			empathy_contact_get_id (contact), iter.element,
			group_name);
		g_signal_emit_by_name (list, "groups-changed", contact,
				       group_name,
				       FALSE);
	}
}

static void
tp_contact_list_group_invalidated_cb (TpChannel *channel,
				      guint      domain,
				      gint       code,
				      gchar     *message,
				      EmpathyTpContactList *list)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	const gchar *group_name;

	group_name = tp_channel_get_identifier (channel);
	DEBUG ("Group %s invalidated. Message: %s", group_name, message);

	tp_contact_list_forget_group (list, channel);

	g_hash_table_remove (priv->groups, group_name);
}

static void
contacts_added_to_group (EmpathyTpContactList *list,
			 TpChannel *channel,
			 GArray *added)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	const gchar *group_name;
	guint i;

	group_name = tp_channel_get_identifier (channel);

	for (i = 0; i < added->len; i++) {
		EmpathyContact *contact;
		TpHandle handle;

		handle = g_array_index (added, TpHandle, i);
		contact = g_hash_table_lookup (priv->members,
					       GUINT_TO_POINTER (handle));
		if (contact == NULL) {
			continue;
		}

		DEBUG ("Contact %s (%d) added to group %s",
			empathy_contact_get_id (contact), handle, group_name);
		g_signal_emit_by_name (list, "groups-changed", contact,
				       group_name,
				       TRUE);
	}
}

static void
tp_contact_list_group_members_changed_cb (TpChannel     *channel,
					  gchar         *message,
					  GArray        *added,
					  GArray        *removed,
					  GArray        *local_pending,
					  GArray        *remote_pending,
					  guint          actor,
					  guint          reason,
					  EmpathyTpContactList *list)
{
	EmpathyTpContactListPriv  *priv = GET_PRIV (list);
	const gchar *group_name;
	guint i;

	contacts_added_to_group (list, channel, added);

	group_name = tp_channel_get_identifier (channel);

	for (i = 0; i < removed->len; i++) {
		EmpathyContact *contact;
		TpHandle handle;

		handle = g_array_index (removed, TpHandle, i);
		contact = g_hash_table_lookup (priv->members,
					       GUINT_TO_POINTER (handle));
		if (contact == NULL) {
			continue;
		}

		DEBUG ("Contact %s (%d) removed from group %s",
			empathy_contact_get_id (contact), handle, group_name);

		g_signal_emit_by_name (list, "groups-changed", contact,
				       group_name,
				       FALSE);
	}
}

static void
tp_contact_list_group_ready_cb (TpChannel *channel,
				const GError *error,
				gpointer list)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	TpChannel *old_group;
	const gchar *group_name;
	const TpIntSet *members;
	GArray *arr;

	if (error) {
		DEBUG ("Error: %s", error->message);
		g_object_unref (channel);
		return;
	}

	group_name = tp_channel_get_identifier (channel);

	/* If there's already a group with this name in the table, we can't
	 * just let it be replaced. Replacing it causes it to be unreffed,
	 * which causes it to be invalidated (see
	 * <https://bugs.freedesktop.org/show_bug.cgi?id=22119>), which causes
	 * it to be removed from the hash table again, which causes it to be
	 * unreffed again.
	 */
	old_group = g_hash_table_lookup (priv->groups, group_name);

	if (old_group != NULL) {
		DEBUG ("Discarding old group %s (%p)", group_name, old_group);
		g_hash_table_steal (priv->groups, group_name);
		tp_contact_list_forget_group (list, old_group);
		g_object_unref (old_group);
	}

	/* Pass the reference on the TpChannel to priv->groups */
	g_hash_table_insert (priv->groups, (gpointer) group_name, channel);
	DEBUG ("Group %s added", group_name);

	g_signal_connect (channel, "group-members-changed",
			  G_CALLBACK (tp_contact_list_group_members_changed_cb),
			  list);

	g_signal_connect (channel, "invalidated",
			  G_CALLBACK (tp_contact_list_group_invalidated_cb),
			  list);

	if (priv->add_to_group) {
		GArray *handles;

		handles = g_hash_table_lookup (priv->add_to_group, group_name);
		if (handles) {
			DEBUG ("Adding initial members to group %s", group_name);
			tp_cli_channel_interface_group_call_add_members (channel,
				-1, handles, NULL, NULL, NULL, NULL, NULL);
			g_hash_table_remove (priv->add_to_group, group_name);
		}
	}

	/* Get initial members of the group */
	members = tp_channel_group_get_members (channel);
	g_assert (members != NULL);
	arr = tp_intset_to_array (members);
	contacts_added_to_group (list, channel, arr);
	g_array_free (arr, TRUE);
}

static void
tp_contact_list_group_add_channel (EmpathyTpContactList *list,
				   const gchar          *object_path,
				   GHashTable           *properties)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	TpChannel                *channel;
	GError *error = NULL;

	channel = tp_channel_new_from_properties (priv->connection,
				  object_path, properties, &error);
	if (channel == NULL) {
		DEBUG ("Failed to create group channel: %s", error->message);
		g_error_free (error);
		return;
	}

	/* Give the ref to the callback */
	tp_channel_call_when_ready (channel,
				    tp_contact_list_group_ready_cb,
				    list);
}

static void
tp_contact_list_group_request_channel_cb (TpConnection *connection,
					  const gchar  *object_path,
					  const GError *error,
					  gpointer      user_data,
					  GObject      *list)
{
	/* The new channel will be handled in NewChannel cb. Here we only
	 * handle the error if RequestChannel failed */
	if (error) {
		DEBUG ("Error: %s", error->message);
		return;
	}
}

static void
tp_contact_list_group_request_handles_cb (TpConnection *connection,
					  const GArray *handles,
					  const GError *error,
					  gpointer      user_data,
					  GObject      *list)
{
	TpHandle channel_handle;

	if (error) {
		DEBUG ("Error: %s", error->message);
		return;
	}

	channel_handle = g_array_index (handles, TpHandle, 0);
	tp_cli_connection_call_request_channel (connection, -1,
						TP_IFACE_CHANNEL_TYPE_CONTACT_LIST,
						TP_HANDLE_TYPE_GROUP,
						channel_handle,
						TRUE,
						tp_contact_list_group_request_channel_cb,
						NULL, NULL,
						list);
}

/* This function takes ownership of handles array */
static void
tp_contact_list_group_add (EmpathyTpContactList *list,
			   const gchar          *group_name,
			   GArray               *handles)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	TpChannel                *channel;
	const gchar              *names[] = {group_name, NULL};

	/* Search the channel for that group name */
	channel = g_hash_table_lookup (priv->groups, group_name);
	if (channel) {
		tp_cli_channel_interface_group_call_add_members (channel, -1,
			handles, NULL, NULL, NULL, NULL, NULL);
		g_array_free (handles, TRUE);
		return;
	}

	/* That group does not exist yet, we have to:
	 * 1) Request an handle for the group name
	 * 2) Request a channel
	 * 3) When NewChannel is emitted, add handles in members
	 */
	g_hash_table_insert (priv->add_to_group,
			     g_strdup (group_name),
			     handles);
	tp_cli_connection_call_request_handles (priv->connection, -1,
						TP_HANDLE_TYPE_GROUP, names,
						tp_contact_list_group_request_handles_cb,
						NULL, NULL,
						G_OBJECT (list));
}

static void
got_added_members_cb (TpConnection            *connection,
		      guint                    n_contacts,
		      EmpathyContact * const * contacts,
		      guint                    n_failed,
		      const TpHandle          *failed,
		      const GError            *error,
		      gpointer                 user_data,
		      GObject                 *list)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	guint i;

	if (error) {
		DEBUG ("Error: %s", error->message);
		return;
	}

	for (i = 0; i < n_contacts; i++) {
		EmpathyContact *contact = contacts[i];
		TpHandle handle = empathy_contact_get_handle (contact);

		if (g_hash_table_lookup (priv->members, GUINT_TO_POINTER (handle)))
			continue;

		/* Add to the list and emit signal */
		g_hash_table_insert (priv->members, GUINT_TO_POINTER (handle),
				     g_object_ref (contact));
		g_signal_emit_by_name (list, "members-changed", contact,
				       0, 0, NULL, TRUE);
	}
}

static void
add_to_members (EmpathyTpContactList *list,
		GArray *handles)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	GArray *request;
	guint i;

	if (handles->len == 0)
		return;

	request = g_array_new (FALSE, FALSE, sizeof (TpHandle));

	for (i = 0; i < handles->len; i++) {
		TpHandle handle = g_array_index (handles, TpHandle, i);

		if (g_hash_table_lookup (priv->members, GUINT_TO_POINTER (handle)))
			continue;

		g_array_append_val (request, handle);
	}

	if (request->len > 0) {
			empathy_tp_contact_factory_get_from_handles (priv->connection,
				request->len, (TpHandle *) request->data,
				got_added_members_cb, NULL, NULL, G_OBJECT (list));
	}

	g_array_free (request, TRUE);
}

static void
tp_contact_list_got_local_pending_cb (TpConnection            *connection,
				      guint                    n_contacts,
				      EmpathyContact * const * contacts,
				      guint                    n_failed,
				      const TpHandle          *failed,
				      const GError            *error,
				      gpointer                 user_data,
				      GObject                 *list)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	guint i;

	if (error) {
		DEBUG ("Error: %s", error->message);
		return;
	}

	for (i = 0; i < n_contacts; i++) {
		EmpathyContact *contact = contacts[i];
		TpHandle handle;
		const gchar *message;
		TpChannelGroupChangeReason reason;
		const TpIntSet *members, *remote_pending;

		handle = empathy_contact_get_handle (contact);
		members = tp_channel_group_get_members (priv->subscribe);
		remote_pending = tp_channel_group_get_remote_pending (priv->subscribe);

		if (tp_intset_is_member (members, handle) ||
		    tp_intset_is_member (remote_pending, handle)) {
			GArray handles = {(gchar *) &handle, 1};

			/* This contact is already subscribed, or user requested
			 * to subscribe, auto accept. */
			tp_cli_channel_interface_group_call_add_members (priv->publish,
				-1, &handles, NULL, NULL, NULL, NULL, NULL);
		}
		else if (tp_channel_group_get_local_pending_info (priv->publish,
								  handle,
								  NULL,
								  &reason,
								  &message)) {
			/* Add contact to pendings */
			g_hash_table_insert (priv->pendings, GUINT_TO_POINTER (handle),
					     g_object_ref (contact));
			g_signal_emit_by_name (list, "pendings-changed", contact,
					       contact, reason, message, TRUE);
		}
	}
}

static void
tp_contact_list_remove_handle (EmpathyTpContactList *list,
			       GHashTable *table,
			       TpHandle handle)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	EmpathyContact *contact;
	const gchar *sig;

	if (table == priv->pendings)
		sig = "pendings-changed";
	else if (table == priv->members)
		sig = "members-changed";
	else
		return;

	contact = g_hash_table_lookup (table, GUINT_TO_POINTER (handle));
	if (contact) {
		g_object_ref (contact);
		g_hash_table_remove (table, GUINT_TO_POINTER (handle));
		g_signal_emit_by_name (list, sig, contact, 0, 0, NULL,
				       FALSE);
		g_object_unref (contact);
	}
}

static void
remove_from_member_if_needed (EmpathyTpContactList *list,
			      TpHandle handle)
{
	/* remove contact from members if it's not in publish and subscribe */
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	const TpIntSet *members;

	members = tp_channel_group_get_members (priv->subscribe);
	if (tp_intset_is_member (members, handle))
		return;

	members = tp_channel_group_get_remote_pending (priv->subscribe);
	if (tp_intset_is_member (members, handle))
		return;

	members = tp_channel_group_get_members (priv->publish);
	if (tp_intset_is_member (members, handle))
		return;

	tp_contact_list_remove_handle (list, priv->members, handle);
}

static void
tp_contact_list_publish_group_members_changed_cb (TpChannel     *channel,
						  gchar         *message,
						  GArray        *added,
						  GArray        *removed,
						  GArray        *local_pending,
						  GArray        *remote_pending,
						  TpHandle       actor,
						  TpChannelGroupChangeReason reason,
						  EmpathyTpContactList *list)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	guint i;

	/* We now send our presence to those contacts, remove them from pendings */
	add_to_members (list, added);
	for (i = 0; i < added->len; i++) {
		tp_contact_list_remove_handle (list, priv->pendings,
			g_array_index (added, TpHandle, i));
	}

	/* We refuse to send our presence to those contacts, remove from pendings */
	for (i = 0; i < removed->len; i++) {
		TpHandle handle = g_array_index (removed, TpHandle, i);

		tp_contact_list_remove_handle (list, priv->pendings, handle);
		remove_from_member_if_needed (list, handle);
	}

	/* Those contacts want our presence, auto accept those that are already
	 * member, otherwise add in pendings. */
	if (local_pending->len > 0) {
		empathy_tp_contact_factory_get_from_handles (priv->connection,
			local_pending->len, (TpHandle *) local_pending->data,
			tp_contact_list_got_local_pending_cb, NULL, NULL,
			G_OBJECT (list));
	}
}

static void
tp_contact_list_get_alias_flags_cb (TpConnection *connection,
				    guint         flags,
				    const GError *error,
				    gpointer      user_data,
				    GObject      *list)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);

	if (error) {
		DEBUG ("Error: %s", error->message);
		return;
	}

	if (flags & TP_CONNECTION_ALIAS_FLAG_USER_SET) {
		priv->flags |= EMPATHY_CONTACT_LIST_CAN_ALIAS;
	}
}

static void
tp_contact_list_get_requestablechannelclasses_cb (TpProxy      *connection,
						  const GValue *value,
						  const GError *error,
						  gpointer      user_data,
						  GObject      *list)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	GPtrArray *classes;
	guint i;

	if (error) {
		DEBUG ("Error: %s", error->message);
		return;
	}

	classes = g_value_get_boxed (value);
	for (i = 0; i < classes->len; i++) {
		GValueArray *class = g_ptr_array_index (classes, i);
		GHashTable *props;
		const char *channel_type;
		guint handle_type;

		props = g_value_get_boxed (g_value_array_get_nth (class, 0));

		channel_type = tp_asv_get_string (props,
				TP_IFACE_CHANNEL ".ChannelType");
		handle_type = tp_asv_get_uint32 (props,
				TP_IFACE_CHANNEL ".TargetHandleType", NULL);

		if (!tp_strdiff (channel_type, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST) &&
		    handle_type == TP_HANDLE_TYPE_GROUP) {
			DEBUG ("Got channel class for a contact group");
			priv->flags |= EMPATHY_CONTACT_LIST_CAN_GROUP;
			break;
		}
	}
}

static void
tp_contact_list_subscribe_group_members_changed_cb (TpChannel     *channel,
						    gchar         *message,
						    GArray        *added,
						    GArray        *removed,
						    GArray        *local_pending,
						    GArray        *remote_pending,
						    guint          actor,
						    guint          reason,
						    EmpathyTpContactList *list)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	guint i;
	GArray *accept;

	/* We now get the presence of those contacts, add them to members */
	add_to_members (list, added);

	/* Those contacts refuse to send us their presence, remove from members. */
	for (i = 0; i < removed->len; i++) {
		remove_from_member_if_needed (list, g_array_index (removed, TpHandle, i));
	}

	/* We want those contacts in our contact list but we don't get their
	 * presence yet. Add to members anyway. */
	add_to_members (list, remote_pending);

	/* Implicitly accept pending request of contacts which are now members. */
	if (priv->publish == NULL)
		return;

	accept = g_array_new (FALSE, FALSE, sizeof (TpHandle));
	for (i = 0; i < added->len; i++) {
		TpHandle handle = g_array_index (added, TpHandle, i);

		if (g_hash_table_lookup (priv->pendings, GUINT_TO_POINTER (handle)) != NULL)
			g_array_append_val (accept, handle);
	}

	for (i = 0; i < remote_pending->len; i++) {
		TpHandle handle = g_array_index (added, TpHandle, i);

		if (g_hash_table_lookup (priv->pendings, GUINT_TO_POINTER (handle)) != NULL)
			g_array_append_val (accept, handle);
	}

	tp_cli_channel_interface_group_call_add_members (priv->publish,
		-1, accept, NULL, NULL, NULL, NULL, NULL);

	g_array_free (accept, TRUE);
}

static void
tp_contact_list_store_group_members_changed_cb (TpChannel     *channel,
						gchar         *message,
						GArray        *added,
						GArray        *removed,
						GArray        *local_pending,
						GArray        *remote_pending,
						guint          actor,
						guint          reason,
						EmpathyTpContactList *list)
{
	guint i;

	add_to_members (list, added);

	for (i = 0; i < removed->len; i++) {
		remove_from_member_if_needed (list, g_array_index (removed, TpHandle, i));
	}
}

static void
tp_contact_list_finalize (GObject *object)
{
	EmpathyTpContactListPriv *priv;
	EmpathyTpContactList     *list;
	GHashTableIter            iter;
	gpointer                  channel;

	list = EMPATHY_TP_CONTACT_LIST (object);
	priv = GET_PRIV (list);

	DEBUG ("finalize: %p", object);

	if (priv->subscribe) {
		g_object_unref (priv->subscribe);
	}
	if (priv->publish) {
		g_object_unref (priv->publish);
	}
	if (priv->stored) {
		g_object_unref (priv->stored);
	}

	if (priv->connection) {
		g_object_unref (priv->connection);
	}

	g_hash_table_iter_init (&iter, priv->groups);
	while (g_hash_table_iter_next (&iter, NULL, &channel)) {
		g_signal_handlers_disconnect_by_func (channel,
			tp_contact_list_group_invalidated_cb, list);
	}

	g_hash_table_destroy (priv->groups);
	g_hash_table_destroy (priv->members);
	g_hash_table_destroy (priv->pendings);
	g_hash_table_destroy (priv->add_to_group);

	G_OBJECT_CLASS (empathy_tp_contact_list_parent_class)->finalize (object);
}

static void
got_list_channel (EmpathyTpContactList *list,
		  TpChannel *channel)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	const gchar *id;

	/* We requested that channel by providing TargetID property, so it's
	 * guaranteed that tp_channel_get_identifier will return it. */
	id = tp_channel_get_identifier (channel);

	/* TpChannel emits initial set of members just before being ready */
	if (!tp_strdiff (id, "stored")) {
		if (priv->stored != NULL)
			return;
		priv->stored = g_object_ref (channel);
		g_signal_connect (priv->stored, "group-members-changed",
				  G_CALLBACK (tp_contact_list_store_group_members_changed_cb),
				  list);
	} else if (!tp_strdiff (id, "publish")) {
		if (priv->publish != NULL)
			return;
		priv->publish = g_object_ref (channel);
		g_signal_connect (priv->publish, "group-members-changed",
				  G_CALLBACK (tp_contact_list_publish_group_members_changed_cb),
				  list);
	} else if (!tp_strdiff (id, "subscribe")) {
		if (priv->subscribe != NULL)
			return;
		priv->subscribe = g_object_ref (channel);
		g_signal_connect (priv->subscribe, "group-members-changed",
				  G_CALLBACK (tp_contact_list_subscribe_group_members_changed_cb),
				  list);
	}
}

static void
list_ensure_channel_cb (TpConnection *conn,
			gboolean yours,
			const gchar *path,
			GHashTable *properties,
			const GError *error,
			gpointer user_data,
			GObject *weak_object)
{
	EmpathyTpContactList *list = user_data;
	TpChannel *channel;

	if (error != NULL) {
		DEBUG ("failed: %s\n", error->message);
		return;
	}

	channel = tp_channel_new_from_properties (conn, path, properties, NULL);
	got_list_channel (list, channel);
	g_object_unref (channel);
}

static void
iterate_on_channels (EmpathyTpContactList *list,
		     const GPtrArray *channels)
{
	guint i;

	for (i = 0; i < channels->len ; i++) {
		GValueArray *arr = g_ptr_array_index (channels, i);
		const gchar *path;
		GHashTable *properties;
		TpHandleType handle_type;

		path = g_value_get_boxed (g_value_array_get_nth (arr, 0));
		properties = g_value_get_boxed (g_value_array_get_nth (arr, 1));

		if (tp_strdiff (tp_asv_get_string (properties,
				TP_IFACE_CHANNEL ".ChannelType"),
		    TP_IFACE_CHANNEL_TYPE_CONTACT_LIST))
			continue;

		if (tp_asv_get_string (properties, TP_IFACE_CHANNEL ".TargetID") == NULL)
			continue;

		handle_type = tp_asv_get_uint32 (properties,
			TP_IFACE_CHANNEL ".TargetHandleType", NULL);

		if (handle_type != TP_HANDLE_TYPE_GROUP)
			continue;

		tp_contact_list_group_add_channel (list, path, properties);
	}
}

static void
new_channels_cb (TpConnection *conn,
		 const GPtrArray *channels,
		 gpointer user_data,
		 GObject *weak_object)
{
	EmpathyTpContactList *list = EMPATHY_TP_CONTACT_LIST (weak_object);

	iterate_on_channels (list, channels);
}

static void
got_channels_cb (TpProxy *conn,
		 const GValue *out,
		 const GError *error,
		 gpointer user_data,
		 GObject *weak_object)
{
	EmpathyTpContactList *list = EMPATHY_TP_CONTACT_LIST (weak_object);
	const GPtrArray *channels;

	if (error != NULL) {
		DEBUG ("Get Channels property failed: %s", error->message);
		return;
	}

	channels = g_value_get_boxed (out);
	iterate_on_channels (list, channels);
}

static void
conn_ready_cb (TpConnection *connection,
	       const GError *error,
	       gpointer data)
{
	EmpathyTpContactList *list = data;
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	GHashTable *request;

	if (error != NULL) {
		DEBUG ("failed: %s", error->message);
		goto out;
	}

	/* Look for existing group channels */
	tp_cli_dbus_properties_call_get (connection, -1,
		TP_IFACE_CONNECTION_INTERFACE_REQUESTS, "Channels", got_channels_cb,
		NULL, NULL, G_OBJECT (list));

	request = tp_asv_new (
		TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST,
		TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_LIST,
		NULL);

	/* Watch the NewChannels signal so if ensuring list channels fails (for
	 * example because the server is slow and the D-Bus call timeouts before CM
	 * fetches the roster), we have a chance to get them later. */
	tp_cli_connection_interface_requests_connect_to_new_channels (
		priv->connection, new_channels_cb, NULL, NULL, G_OBJECT (list), NULL);

	/* Request the 'stored' list. */
	tp_asv_set_static_string (request, TP_PROP_CHANNEL_TARGET_ID, "stored");
	tp_cli_connection_interface_requests_call_ensure_channel (priv->connection,
		G_MAXINT, request, list_ensure_channel_cb, list, NULL, G_OBJECT (list));

	/* Request the 'publish' list. */
	tp_asv_set_static_string (request, TP_PROP_CHANNEL_TARGET_ID, "publish");
	tp_cli_connection_interface_requests_call_ensure_channel (priv->connection,
		G_MAXINT, request, list_ensure_channel_cb, list, NULL, G_OBJECT (list));

	/* Request the 'subscribe' list. */
	tp_asv_set_static_string (request, TP_PROP_CHANNEL_TARGET_ID, "subscribe");
	tp_cli_connection_interface_requests_call_ensure_channel (priv->connection,
		G_MAXINT, request, list_ensure_channel_cb, list, NULL, G_OBJECT (list));

	g_hash_table_unref (request);

out:
	g_object_unref (list);
}

static void
tp_contact_list_constructed (GObject *list)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);

	/* call GetAliasFlags */
	if (tp_proxy_has_interface_by_id (priv->connection,
				TP_IFACE_QUARK_CONNECTION_INTERFACE_ALIASING)) {
		tp_cli_connection_interface_aliasing_call_get_alias_flags (
				priv->connection,
				-1,
				tp_contact_list_get_alias_flags_cb,
				NULL, NULL,
				G_OBJECT (list));
	}

	/* lookup RequestableChannelClasses */
	if (tp_proxy_has_interface_by_id (priv->connection,
				TP_IFACE_QUARK_CONNECTION_INTERFACE_REQUESTS)) {
		tp_cli_dbus_properties_call_get (priv->connection,
				-1,
				TP_IFACE_CONNECTION_INTERFACE_REQUESTS,
				"RequestableChannelClasses",
				tp_contact_list_get_requestablechannelclasses_cb,
				NULL, NULL,
				G_OBJECT (list));
	} else {
		/* we just don't know... better mark the flag just in case */
		priv->flags |= EMPATHY_CONTACT_LIST_CAN_GROUP;
	}

	tp_connection_call_when_ready (priv->connection, conn_ready_cb,
		g_object_ref (list));
}

static void
tp_contact_list_get_property (GObject    *object,
			      guint       param_id,
			      GValue     *value,
			      GParamSpec *pspec)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_CONNECTION:
		g_value_set_object (value, priv->connection);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
tp_contact_list_set_property (GObject      *object,
			      guint         param_id,
			      const GValue *value,
			      GParamSpec   *pspec)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_CONNECTION:
		priv->connection = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
empathy_tp_contact_list_class_init (EmpathyTpContactListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tp_contact_list_finalize;
	object_class->constructed = tp_contact_list_constructed;
	object_class->get_property = tp_contact_list_get_property;
	object_class->set_property = tp_contact_list_set_property;

	g_object_class_install_property (object_class,
					 PROP_CONNECTION,
					 g_param_spec_object ("connection",
							      "The Connection",
							      "The connection associated with the contact list",
							      TP_TYPE_CONNECTION,
							      G_PARAM_READWRITE |
							      G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (object_class, sizeof (EmpathyTpContactListPriv));
}

static void
tp_contact_list_array_free (gpointer handles)
{
	g_array_free (handles, TRUE);
}

static void
empathy_tp_contact_list_init (EmpathyTpContactList *list)
{
	EmpathyTpContactListPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (list,
		EMPATHY_TYPE_TP_CONTACT_LIST, EmpathyTpContactListPriv);

	list->priv = priv;

	/* Map group's name to group's TpChannel. The group name string is owned
	 * by the TpChannel object */
	priv->groups = g_hash_table_new_full (g_str_hash, g_str_equal,
					      NULL,
					      (GDestroyNotify) g_object_unref);

	/* Map contact's handle to EmpathyContact object */
	priv->members = g_hash_table_new_full (g_direct_hash, g_direct_equal,
					       NULL,
					       (GDestroyNotify) g_object_unref);

	/* Map contact's handle to EmpathyContact object */
	priv->pendings = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						NULL,
						(GDestroyNotify) g_object_unref);

	/* Map group's name to GArray of handle */
	priv->add_to_group = g_hash_table_new_full (g_str_hash, g_str_equal,
						    g_free,
						    tp_contact_list_array_free);
}

EmpathyTpContactList *
empathy_tp_contact_list_new (TpConnection *connection)
{
	return g_object_new (EMPATHY_TYPE_TP_CONTACT_LIST,
			     "connection", connection,
			     NULL);
}

TpConnection *
empathy_tp_contact_list_get_connection (EmpathyTpContactList *list)
{
	EmpathyTpContactListPriv *priv;

	g_return_val_if_fail (EMPATHY_IS_TP_CONTACT_LIST (list), NULL);

	priv = GET_PRIV (list);

	return priv->connection;
}

static void
tp_contact_list_add (EmpathyContactList *list,
		     EmpathyContact     *contact,
		     const gchar        *message)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	TpHandle handle;
	GArray handles = {(gchar *) &handle, 1};

	handle = empathy_contact_get_handle (contact);

	if (priv->subscribe) {
		tp_cli_channel_interface_group_call_add_members (priv->subscribe,
			-1, &handles, message, NULL, NULL, NULL, NULL);
	}

	if (priv->publish) {
		TpChannelGroupFlags flags = tp_channel_group_get_flags (priv->subscribe);
		if (flags & TP_CHANNEL_GROUP_FLAG_CAN_ADD ||
		    g_hash_table_lookup (priv->pendings, GUINT_TO_POINTER (handle))) {
			tp_cli_channel_interface_group_call_add_members (priv->publish,
				-1, &handles, message, NULL, NULL, NULL, NULL);
		}
	}

	/* We want to unblock the contact */
	if (tp_proxy_has_interface_by_id (priv->connection,
		TP_IFACE_QUARK_CONNECTION_INTERFACE_CONTACT_BLOCKING)) {
		TpContact *tp_contact = empathy_contact_get_tp_contact (contact);

		if (tp_contact != NULL)
			tp_contact_unblock_async (tp_contact, NULL, NULL);
	}
}

static void
tp_contact_list_remove (EmpathyContactList *list,
			EmpathyContact     *contact,
			const gchar        *message)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	TpHandle handle;
	GArray handles = {(gchar *) &handle, 1};

	handle = empathy_contact_get_handle (contact);

	/* FIXME: this is racy if tp_contact_list_remove is called before the
	 * 'stored' list has been retrieved. */
	if (priv->stored != NULL) {
		tp_cli_channel_interface_group_call_remove_members (priv->stored,
			-1, &handles, message, NULL, NULL, NULL, NULL);
	}

	if (priv->subscribe) {
		tp_cli_channel_interface_group_call_remove_members (priv->subscribe,
			-1, &handles, message, NULL, NULL, NULL, NULL);
	}
	if (priv->publish) {
		tp_cli_channel_interface_group_call_remove_members (priv->publish,
			-1, &handles, message, NULL, NULL, NULL, NULL);
	}
}

static GList *
tp_contact_list_get_members (EmpathyContactList *list)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	GList *ret;

	ret = g_hash_table_get_values (priv->members);
	g_list_foreach (ret, (GFunc) g_object_ref, NULL);
	return ret;
}

static GList *
tp_contact_list_get_pendings (EmpathyContactList *list)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	GList *ret;

	ret = g_hash_table_get_values (priv->pendings);
	g_list_foreach (ret, (GFunc) g_object_ref, NULL);
	return ret;
}

static GList *
tp_contact_list_get_all_groups (EmpathyContactList *list)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	GList                    *ret, *l;

	ret = g_hash_table_get_keys (priv->groups);
	for (l = ret; l; l = l->next) {
		l->data = g_strdup (l->data);
	}

	return ret;
}

static GList *
tp_contact_list_get_groups (EmpathyContactList *list,
			    EmpathyContact     *contact)
{
	EmpathyTpContactListPriv  *priv = GET_PRIV (list);
	GList                     *ret = NULL;
	GHashTableIter             iter;
	gpointer                   group_name;
	gpointer                   channel;
	TpHandle                   handle;

	handle = empathy_contact_get_handle (contact);
	g_hash_table_iter_init (&iter, priv->groups);
	while (g_hash_table_iter_next (&iter, &group_name, &channel)) {
		const TpIntSet *members;

		members = tp_channel_group_get_members (channel);
		if (tp_intset_is_member (members, handle)) {
			ret = g_list_prepend (ret, g_strdup (group_name));
		}
	}

	return ret;
}

static void
tp_contact_list_add_to_group (EmpathyContactList *list,
			      EmpathyContact     *contact,
			      const gchar        *group_name)
{
	TpHandle handle;
	GArray *handles;

	handle = empathy_contact_get_handle (contact);
	handles = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 1);
	g_array_append_val (handles, handle);
	tp_contact_list_group_add (EMPATHY_TP_CONTACT_LIST (list),
				   group_name, handles);
}

static void
tp_contact_list_remove_from_group (EmpathyContactList *list,
				   EmpathyContact     *contact,
				   const gchar        *group_name)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	TpChannel                *channel;
	TpHandle                  handle;
	GArray                    handles = {(gchar *) &handle, 1};

	channel = g_hash_table_lookup (priv->groups, group_name);
	if (channel == NULL) {
		return;
	}

	handle = empathy_contact_get_handle (contact);
	DEBUG ("remove contact %s (%d) from group %s",
		empathy_contact_get_id (contact), handle, group_name);

	tp_cli_channel_interface_group_call_remove_members (channel, -1,
		&handles, NULL, NULL, NULL, NULL, NULL);
}

static void
tp_contact_list_rename_group (EmpathyContactList *list,
			      const gchar        *old_group_name,
			      const gchar        *new_group_name)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	TpChannel                *channel;
	const TpIntSet           *members;
	GArray                   *handles;

	channel = g_hash_table_lookup (priv->groups, old_group_name);
	if (channel == NULL) {
		return;
	}

	DEBUG ("rename group %s to %s", old_group_name, new_group_name);

	/* Remove all members and close the old channel */
	members = tp_channel_group_get_members (channel);
	handles = tp_intset_to_array (members);
	tp_cli_channel_interface_group_call_remove_members (channel, -1,
		handles, NULL, NULL, NULL, NULL, NULL);
	tp_cli_channel_call_close (channel, -1, NULL, NULL, NULL, NULL);

	tp_contact_list_group_add (EMPATHY_TP_CONTACT_LIST (list),
				   new_group_name, handles);
}

static void
tp_contact_list_remove_group (EmpathyContactList *list,
			      const gchar *group_name)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	TpChannel                *channel;
	const TpIntSet           *members;
	GArray                   *handles;

	channel = g_hash_table_lookup (priv->groups, group_name);
	if (channel == NULL) {
		return;
	}

	DEBUG ("remove group %s", group_name);

	/* Remove all members and close the channel */
	members = tp_channel_group_get_members (channel);
	handles = tp_intset_to_array (members);
	tp_cli_channel_interface_group_call_remove_members (channel, -1,
		handles, NULL, NULL, NULL, NULL, NULL);
	tp_cli_channel_call_close (channel, -1, NULL, NULL, NULL, NULL);
	g_array_free (handles, TRUE);
}

static EmpathyContactListFlags
tp_contact_list_get_flags (EmpathyContactList *list)
{
	EmpathyTpContactListPriv *priv;
	EmpathyContactListFlags flags;

	g_return_val_if_fail (EMPATHY_IS_TP_CONTACT_LIST (list), FALSE);

	priv = GET_PRIV (list);
	flags = priv->flags;

	if (priv->subscribe != NULL) {
		TpChannelGroupFlags group_flags;

		group_flags = tp_channel_group_get_flags (priv->subscribe);

		if (group_flags & TP_CHANNEL_GROUP_FLAG_CAN_ADD) {
			flags |= EMPATHY_CONTACT_LIST_CAN_ADD;
		}

		if (group_flags & TP_CHANNEL_GROUP_FLAG_CAN_REMOVE) {
			flags |= EMPATHY_CONTACT_LIST_CAN_REMOVE;
		}

		if (group_flags & TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD) {
			flags |= EMPATHY_CONTACT_LIST_MESSAGE_ADD;
		}
	}

	return flags;
}

static void
tp_contact_list_iface_init (EmpathyContactListIface *iface)
{
	iface->add               = tp_contact_list_add;
	iface->remove            = tp_contact_list_remove;
	iface->get_members       = tp_contact_list_get_members;
	iface->get_pendings      = tp_contact_list_get_pendings;
	iface->get_all_groups    = tp_contact_list_get_all_groups;
	iface->get_groups        = tp_contact_list_get_groups;
	iface->add_to_group      = tp_contact_list_add_to_group;
	iface->remove_from_group = tp_contact_list_remove_from_group;
	iface->rename_group      = tp_contact_list_rename_group;
	iface->remove_group	 = tp_contact_list_remove_group;
	iface->get_flags	 = tp_contact_list_get_flags;
}

void
empathy_tp_contact_list_remove_all (EmpathyTpContactList *list)
{
	EmpathyTpContactListPriv *priv = GET_PRIV (list);
	GHashTableIter            iter;
	gpointer                  contact;

	g_return_if_fail (EMPATHY_IS_TP_CONTACT_LIST (list));

	/* Remove all contacts */
	g_hash_table_iter_init (&iter, priv->members);
	while (g_hash_table_iter_next (&iter, NULL, &contact)) {
		g_signal_emit_by_name (list, "members-changed", contact,
				       NULL, 0, NULL,
				       FALSE);
	}
	g_hash_table_remove_all (priv->members);

	g_hash_table_iter_init (&iter, priv->pendings);
	while (g_hash_table_iter_next (&iter, NULL, &contact)) {
		g_signal_emit_by_name (list, "pendings-changed", contact,
				       NULL, 0, NULL,
				       FALSE);
	}
	g_hash_table_remove_all (priv->pendings);
}
