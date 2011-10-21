/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2007-2008 Collabora Ltd.
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
 * Authors: Mikael Hallendal <micke@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 */

#include "config.h"

#include <string.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <telepathy-glib/util.h>

#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-tp-chat.h>
#include <libempathy/empathy-enum-types.h>
#include <libempathy/empathy-contact-manager.h>

#include "empathy-contact-list-store.h"
#include "empathy-ui-utils.h"
#include "empathy-gtk-enum-types.h"
#include "empathy-images.h"

#define DEBUG_FLAG EMPATHY_DEBUG_CONTACT
#include <libempathy/empathy-debug.h>

/* Active users are those which have recently changed state
 * (e.g. online, offline or from normal to a busy state).
 */

/* Time in seconds user is shown as active */
#define ACTIVE_USER_SHOW_TIME 7

/* Time in seconds after connecting which we wait before active users are enabled */
#define ACTIVE_USER_WAIT_TO_ENABLE_TIME 5

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyContactListStore)
typedef struct {
	EmpathyContactList         *list;
	gboolean                    show_offline;
	gboolean                    show_avatars;
	gboolean                    show_groups;
	gboolean                    is_compact;
	gboolean                    show_protocols;
	gboolean                    show_active;
	EmpathyContactListStoreSort sort_criterium;
	guint                       inhibit_active;
	guint                       setup_idle_id;
	gboolean                    dispose_has_run;
	GHashTable                  *status_icons;
	/* Hash: EmpathyContact* -> GQueue (GtkTreeIter *) */
	GHashTable                  *empathy_contact_cache;
	/* Hash: char *groupname -> GtkTreeIter * */
	GHashTable                  *empathy_group_cache;
} EmpathyContactListStorePriv;

typedef struct {
	EmpathyContactListStore *store;
	EmpathyContact          *contact;
	gboolean                remove;
} ShowActiveData;

static void             contact_list_store_dispose                  (GObject                       *object);
static void             contact_list_store_get_property              (GObject                       *object,
								      guint                          param_id,
								      GValue                        *value,
								      GParamSpec                    *pspec);
static void             contact_list_store_set_property              (GObject                       *object,
								      guint                          param_id,
								      const GValue                  *value,
								      GParamSpec                    *pspec);
static void             contact_list_store_setup                     (EmpathyContactListStore       *store);
static gboolean         contact_list_store_inibit_active_cb          (EmpathyContactListStore       *store);
static void             contact_list_store_members_changed_cb        (EmpathyContactList            *list_iface,
								      EmpathyContact                *contact,
								      EmpathyContact                *actor,
								      guint                          reason,
								      gchar                         *message,
								      gboolean                       is_member,
								      EmpathyContactListStore       *store);
static void             contact_list_store_member_renamed_cb         (EmpathyContactList            *list_iface,
								      EmpathyContact                *old_contact,
								      EmpathyContact                *new_contact,
								      guint                          reason,
								      gchar                         *message,
								      EmpathyContactListStore       *store);
static void             contact_list_store_groups_changed_cb         (EmpathyContactList            *list_iface,
								      EmpathyContact                *contact,
								      gchar                         *group,
								      gboolean                       is_member,
								      EmpathyContactListStore       *store);
static void             contact_list_store_add_contact               (EmpathyContactListStore       *store,
								      EmpathyContact                *contact);
static void             contact_list_store_remove_contact            (EmpathyContactListStore       *store,
								      EmpathyContact                *contact);
static void             contact_list_store_contact_update            (EmpathyContactListStore       *store,
								      EmpathyContact                *contact);
static void             contact_list_store_contact_updated_cb        (EmpathyContact                *contact,
								      GParamSpec                    *param,
								      EmpathyContactListStore       *store);
static void             contact_list_store_contact_set_active        (EmpathyContactListStore       *store,
								      EmpathyContact                *contact,
								      gboolean                       active,
								      gboolean                       set_changed);
static ShowActiveData * contact_list_store_contact_active_new        (EmpathyContactListStore       *store,
								      EmpathyContact                *contact,
								      gboolean                       remove);
static void             contact_list_store_contact_active_free       (ShowActiveData                *data);
static gboolean         contact_list_store_contact_active_cb         (ShowActiveData                *data);
static void             contact_list_store_get_group                 (EmpathyContactListStore       *store,
								      const gchar                   *name,
								      GtkTreeIter                   *iter_group_to_set,
								      GtkTreeIter                   *iter_separator_to_set,
								      gboolean                      *created,
								      gboolean                      is_fake_group);
static gint             contact_list_store_state_sort_func           (GtkTreeModel                  *model,
								      GtkTreeIter                   *iter_a,
								      GtkTreeIter                   *iter_b,
								      gpointer                       user_data);
static gint             contact_list_store_name_sort_func            (GtkTreeModel                  *model,
								      GtkTreeIter                   *iter_a,
								      GtkTreeIter                   *iter_b,
								      gpointer                       user_data);
static GList *          contact_list_store_find_contact              (EmpathyContactListStore       *store,
								      EmpathyContact                *contact);
static gboolean         contact_list_store_update_list_mode_foreach  (GtkTreeModel                  *model,
								      GtkTreePath                   *path,
								      GtkTreeIter                   *iter,
								      EmpathyContactListStore       *store);

enum {
	PROP_0,
	PROP_CONTACT_LIST,
	PROP_SHOW_OFFLINE,
	PROP_SHOW_AVATARS,
	PROP_SHOW_PROTOCOLS,
	PROP_SHOW_GROUPS,
	PROP_IS_COMPACT,
	PROP_SORT_CRITERIUM
};

G_DEFINE_TYPE (EmpathyContactListStore, empathy_contact_list_store, GTK_TYPE_TREE_STORE);

static void
contact_list_store_chat_state_changed_cb (TpChannel *self,
				     guint contact_handle,
				     guint state,
				     gpointer store)
{
	EmpathyContactListStorePriv *priv = GET_PRIV (store);
	GList *contacts, *l;

	contacts = empathy_contact_list_get_members (priv->list);

	for (l = contacts; l != NULL; l = l->next) {
		EmpathyContact *contact = EMPATHY_CONTACT (l->data);

		if (empathy_contact_get_handle (contact) == contact_handle) {
			contact_list_store_contact_update (store, contact);
			break;
		}
	}

	g_list_foreach (contacts, (GFunc) g_object_unref, NULL);
	g_list_free (contacts);
}

static gboolean
contact_list_store_iface_setup (gpointer user_data)
{
	EmpathyContactListStore     *store = user_data;
	EmpathyContactListStorePriv *priv = GET_PRIV (store);
	GList                       *contacts, *l;

	/* Signal connection. */
	g_signal_connect (priv->list,
			  "member-renamed",
			  G_CALLBACK (contact_list_store_member_renamed_cb),
			  store);
	g_signal_connect (priv->list,
			  "members-changed",
			  G_CALLBACK (contact_list_store_members_changed_cb),
			  store);
	g_signal_connect (priv->list,
			  "groups-changed",
			  G_CALLBACK (contact_list_store_groups_changed_cb),
			  store);

	if (EMPATHY_IS_TP_CHAT (priv->list)) {
			g_signal_connect (priv->list,
					  "chat-state-changed",
					  G_CALLBACK (contact_list_store_chat_state_changed_cb),
					  store);
	}

	/* Add contacts already created. */
	contacts = empathy_contact_list_get_members (priv->list);
	for (l = contacts; l; l = l->next) {
		contact_list_store_members_changed_cb (priv->list, l->data,
						       NULL, 0, NULL,
						       TRUE,
						       store);

		g_object_unref (l->data);
	}
	g_list_free (contacts);

	priv->setup_idle_id = 0;
	return FALSE;
}


static void
contact_list_store_set_contact_list (EmpathyContactListStore *store,
				     EmpathyContactList      *list_iface)
{
	EmpathyContactListStorePriv *priv = GET_PRIV (store);

	priv->list = g_object_ref (list_iface);

	/* Let a chance to have all properties set before populating */
	priv->setup_idle_id = g_idle_add (contact_list_store_iface_setup, store);
}

static void
empathy_contact_list_store_class_init (EmpathyContactListStoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = contact_list_store_dispose;
	object_class->get_property = contact_list_store_get_property;
	object_class->set_property = contact_list_store_set_property;

	g_object_class_install_property (object_class,
					 PROP_CONTACT_LIST,
					 g_param_spec_object ("contact-list",
							      "The contact list iface",
							      "The contact list iface",
							      EMPATHY_TYPE_CONTACT_LIST,
							      G_PARAM_CONSTRUCT_ONLY |
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_SHOW_OFFLINE,
					 g_param_spec_boolean ("show-offline",
							       "Show Offline",
							       "Whether contact list should display "
							       "offline contacts",
							       FALSE,
							       G_PARAM_READWRITE));
	 g_object_class_install_property (object_class,
					  PROP_SHOW_AVATARS,
					  g_param_spec_boolean ("show-avatars",
								"Show Avatars",
								"Whether contact list should display "
								"avatars for contacts",
								TRUE,
								G_PARAM_READWRITE));
	 g_object_class_install_property (object_class,
					  PROP_SHOW_PROTOCOLS,
					  g_param_spec_boolean ("show-protocols",
								"Show Protocols",
								"Whether contact list should display "
								"protocols for contacts",
								FALSE,
								G_PARAM_READWRITE));
	 g_object_class_install_property (object_class,
					  PROP_SHOW_GROUPS,
					  g_param_spec_boolean ("show-groups",
								"Show Groups",
								"Whether contact list should display "
								"contact groups",
								TRUE,
								G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_IS_COMPACT,
					 g_param_spec_boolean ("is-compact",
							       "Is Compact",
							       "Whether the contact list is in compact mode or not",
							       FALSE,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_SORT_CRITERIUM,
					 g_param_spec_enum ("sort-criterium",
							    "Sort citerium",
							    "The sort criterium to use for sorting the contact list",
							    EMPATHY_TYPE_CONTACT_LIST_STORE_SORT,
							    EMPATHY_CONTACT_LIST_STORE_SORT_NAME,
							    G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof (EmpathyContactListStorePriv));
}

static void
g_queue_free_full_iter (gpointer data)
{
	GQueue *queue = (GQueue *) data;
	g_queue_foreach (queue, (GFunc) gtk_tree_iter_free, NULL);
	g_queue_free (queue);
}


static void
empathy_contact_list_store_init (EmpathyContactListStore *store)
{
	EmpathyContactListStorePriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (store,
		EMPATHY_TYPE_CONTACT_LIST_STORE, EmpathyContactListStorePriv);

	store->priv = priv;
	priv->show_avatars = TRUE;
	priv->show_groups = TRUE;
	priv->show_protocols = FALSE;
	priv->inhibit_active = g_timeout_add_seconds (ACTIVE_USER_WAIT_TO_ENABLE_TIME,
						      (GSourceFunc) contact_list_store_inibit_active_cb,
						      store);
	priv->status_icons = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	priv->empathy_contact_cache = g_hash_table_new_full (NULL, NULL, NULL,
		g_queue_free_full_iter);
	priv->empathy_group_cache = g_hash_table_new_full (g_str_hash,
		g_str_equal, g_free,
		(GDestroyNotify) gtk_tree_iter_free);
	contact_list_store_setup (store);
}

static void
contact_list_store_dispose (GObject *object)
{
	EmpathyContactListStorePriv *priv = GET_PRIV (object);
	GList                       *contacts, *l;

	if (priv->dispose_has_run)
		return;
	priv->dispose_has_run = TRUE;

	contacts = empathy_contact_list_get_members (priv->list);
	for (l = contacts; l; l = l->next) {
		g_signal_handlers_disconnect_by_func (l->data,
						      G_CALLBACK (contact_list_store_contact_updated_cb),
						      object);

		g_object_unref (l->data);
	}
	g_list_free (contacts);

	g_signal_handlers_disconnect_by_func (priv->list,
					      G_CALLBACK (contact_list_store_member_renamed_cb),
					      object);
	g_signal_handlers_disconnect_by_func (priv->list,
					      G_CALLBACK (contact_list_store_members_changed_cb),
					      object);
	g_signal_handlers_disconnect_by_func (priv->list,
					      G_CALLBACK (contact_list_store_groups_changed_cb),
					      object);
	g_object_unref (priv->list);

	if (priv->inhibit_active != 0) {
		g_source_remove (priv->inhibit_active);
		priv->inhibit_active = 0;
	}

	if (priv->setup_idle_id != 0) {
		g_source_remove (priv->setup_idle_id);
		priv->setup_idle_id = 0;
	}

	g_hash_table_destroy (priv->status_icons);
	g_hash_table_destroy (priv->empathy_contact_cache);
	g_hash_table_destroy (priv->empathy_group_cache);
	G_OBJECT_CLASS (empathy_contact_list_store_parent_class)->dispose (object);
}

static void
contact_list_store_get_property (GObject    *object,
				 guint       param_id,
				 GValue     *value,
				 GParamSpec *pspec)
{
	EmpathyContactListStorePriv *priv;

	priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_CONTACT_LIST:
		g_value_set_object (value, priv->list);
		break;
	case PROP_SHOW_OFFLINE:
		g_value_set_boolean (value, priv->show_offline);
		break;
	case PROP_SHOW_AVATARS:
		g_value_set_boolean (value, priv->show_avatars);
		break;
	case PROP_SHOW_PROTOCOLS:
		g_value_set_boolean (value, priv->show_protocols);
		break;
	case PROP_SHOW_GROUPS:
		g_value_set_boolean (value, priv->show_groups);
		break;
	case PROP_IS_COMPACT:
		g_value_set_boolean (value, priv->is_compact);
		break;
	case PROP_SORT_CRITERIUM:
		g_value_set_enum (value, priv->sort_criterium);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
contact_list_store_set_property (GObject      *object,
				 guint         param_id,
				 const GValue *value,
				 GParamSpec   *pspec)
{
	switch (param_id) {
	case PROP_CONTACT_LIST:
		contact_list_store_set_contact_list (EMPATHY_CONTACT_LIST_STORE (object),
						     g_value_get_object (value));
		break;
	case PROP_SHOW_OFFLINE:
		empathy_contact_list_store_set_show_offline (EMPATHY_CONTACT_LIST_STORE (object),
							    g_value_get_boolean (value));
		break;
	case PROP_SHOW_AVATARS:
		empathy_contact_list_store_set_show_avatars (EMPATHY_CONTACT_LIST_STORE (object),
							    g_value_get_boolean (value));
		break;
	case PROP_SHOW_PROTOCOLS:
		empathy_contact_list_store_set_show_protocols (EMPATHY_CONTACT_LIST_STORE (object),
							    g_value_get_boolean (value));
		break;
	case PROP_SHOW_GROUPS:
		empathy_contact_list_store_set_show_groups (EMPATHY_CONTACT_LIST_STORE (object),
							    g_value_get_boolean (value));
		break;
	case PROP_IS_COMPACT:
		empathy_contact_list_store_set_is_compact (EMPATHY_CONTACT_LIST_STORE (object),
							  g_value_get_boolean (value));
		break;
	case PROP_SORT_CRITERIUM:
		empathy_contact_list_store_set_sort_criterium (EMPATHY_CONTACT_LIST_STORE (object),
							      g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

EmpathyContactListStore *
empathy_contact_list_store_new (EmpathyContactList *list_iface)
{
	g_return_val_if_fail (EMPATHY_IS_CONTACT_LIST (list_iface), NULL);

	return g_object_new (EMPATHY_TYPE_CONTACT_LIST_STORE,
			     "contact-list", list_iface,
			     NULL);
}

EmpathyContactList *
empathy_contact_list_store_get_list_iface (EmpathyContactListStore *store)
{
	EmpathyContactListStorePriv *priv;

	g_return_val_if_fail (EMPATHY_IS_CONTACT_LIST_STORE (store), FALSE);

	priv = GET_PRIV (store);

	return priv->list;
}

gboolean
empathy_contact_list_store_get_show_offline (EmpathyContactListStore *store)
{
	EmpathyContactListStorePriv *priv;

	g_return_val_if_fail (EMPATHY_IS_CONTACT_LIST_STORE (store), FALSE);

	priv = GET_PRIV (store);

	return priv->show_offline;
}

void
empathy_contact_list_store_set_show_offline (EmpathyContactListStore *store,
					    gboolean                show_offline)
{
	EmpathyContactListStorePriv *priv;
	GList                      *contacts, *l;
	gboolean                    show_active;

	g_return_if_fail (EMPATHY_IS_CONTACT_LIST_STORE (store));

	priv = GET_PRIV (store);

	priv->show_offline = show_offline;
	show_active = priv->show_active;

	/* Disable temporarily. */
	priv->show_active = FALSE;

	contacts = empathy_contact_list_get_members (priv->list);
	for (l = contacts; l; l = l->next) {
		contact_list_store_contact_update (store, l->data);

		g_object_unref (l->data);
	}
	g_list_free (contacts);

	/* Restore to original setting. */
	priv->show_active = show_active;

	g_object_notify (G_OBJECT (store), "show-offline");
}

gboolean
empathy_contact_list_store_get_show_avatars (EmpathyContactListStore *store)
{
	EmpathyContactListStorePriv *priv;

	g_return_val_if_fail (EMPATHY_IS_CONTACT_LIST_STORE (store), TRUE);

	priv = GET_PRIV (store);

	return priv->show_avatars;
}

void
empathy_contact_list_store_set_show_avatars (EmpathyContactListStore *store,
					    gboolean                show_avatars)
{
	EmpathyContactListStorePriv *priv;
	GtkTreeModel               *model;

	g_return_if_fail (EMPATHY_IS_CONTACT_LIST_STORE (store));

	priv = GET_PRIV (store);

	priv->show_avatars = show_avatars;

	model = GTK_TREE_MODEL (store);

	gtk_tree_model_foreach (model,
				(GtkTreeModelForeachFunc)
				contact_list_store_update_list_mode_foreach,
				store);

	g_object_notify (G_OBJECT (store), "show-avatars");
}


gboolean
empathy_contact_list_store_get_show_protocols (EmpathyContactListStore *store)
{
	EmpathyContactListStorePriv *priv;

	g_return_val_if_fail (EMPATHY_IS_CONTACT_LIST_STORE (store), TRUE);

	priv = GET_PRIV (store);

	return priv->show_protocols;
}

void
empathy_contact_list_store_set_show_protocols (EmpathyContactListStore *store,
					    gboolean                show_protocols)
{
	EmpathyContactListStorePriv *priv;
	GtkTreeModel               *model;

	g_return_if_fail (EMPATHY_IS_CONTACT_LIST_STORE (store));

	priv = GET_PRIV (store);

	priv->show_protocols = show_protocols;

	model = GTK_TREE_MODEL (store);

	gtk_tree_model_foreach (model,
				(GtkTreeModelForeachFunc)
				contact_list_store_update_list_mode_foreach,
				store);

	g_object_notify (G_OBJECT (store), "show-protocols");
}

gboolean
empathy_contact_list_store_get_show_groups (EmpathyContactListStore *store)
{
	EmpathyContactListStorePriv *priv;

	g_return_val_if_fail (EMPATHY_IS_CONTACT_LIST_STORE (store), TRUE);

	priv = GET_PRIV (store);

	return priv->show_groups;
}

void
empathy_contact_list_store_set_show_groups (EmpathyContactListStore *store,
					    gboolean                 show_groups)
{
	EmpathyContactListStorePriv *priv;

	g_return_if_fail (EMPATHY_IS_CONTACT_LIST_STORE (store));

	priv = GET_PRIV (store);

	if (priv->show_groups == show_groups) {
		return;
	}

	priv->show_groups = show_groups;

	if (priv->setup_idle_id == 0) {
		/* Remove all contacts and add them back, not optimized but
		 * that's the easy way :)
		 *
		 * This is only done if there's not a pending setup idle
		 * callback, otherwise it will race and the contacts will get
		 * added twice */
		GList *contacts, *l;

		gtk_tree_store_clear (GTK_TREE_STORE (store));

		/* Also clear the cache */
		g_hash_table_remove_all (priv->empathy_contact_cache);
		g_hash_table_remove_all (priv->empathy_group_cache);

		contacts = empathy_contact_list_get_members (priv->list);
		for (l = contacts; l; l = l->next) {
			contact_list_store_members_changed_cb (priv->list,
							       l->data,
							       NULL, 0, NULL,
							       TRUE,
							       store);

			g_object_unref (l->data);
		}
		g_list_free (contacts);
	}

	g_object_notify (G_OBJECT (store), "show-groups");
}

gboolean
empathy_contact_list_store_get_is_compact (EmpathyContactListStore *store)
{
	EmpathyContactListStorePriv *priv;

	g_return_val_if_fail (EMPATHY_IS_CONTACT_LIST_STORE (store), TRUE);

	priv = GET_PRIV (store);

	return priv->is_compact;
}

void
empathy_contact_list_store_set_is_compact (EmpathyContactListStore *store,
					  gboolean                is_compact)
{
	EmpathyContactListStorePriv *priv;
	GtkTreeModel               *model;

	g_return_if_fail (EMPATHY_IS_CONTACT_LIST_STORE (store));

	priv = GET_PRIV (store);

	priv->is_compact = is_compact;

	model = GTK_TREE_MODEL (store);

	gtk_tree_model_foreach (model,
				(GtkTreeModelForeachFunc)
				contact_list_store_update_list_mode_foreach,
				store);

	g_object_notify (G_OBJECT (store), "is-compact");
}

EmpathyContactListStoreSort
empathy_contact_list_store_get_sort_criterium (EmpathyContactListStore *store)
{
	EmpathyContactListStorePriv *priv;

	g_return_val_if_fail (EMPATHY_IS_CONTACT_LIST_STORE (store), 0);

	priv = GET_PRIV (store);

	return priv->sort_criterium;
}

void
empathy_contact_list_store_set_sort_criterium (EmpathyContactListStore     *store,
					      EmpathyContactListStoreSort  sort_criterium)
{
	EmpathyContactListStorePriv *priv;

	g_return_if_fail (EMPATHY_IS_CONTACT_LIST_STORE (store));

	priv = GET_PRIV (store);

	priv->sort_criterium = sort_criterium;

	switch (sort_criterium) {
	case EMPATHY_CONTACT_LIST_STORE_SORT_STATE:
		gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
						      EMPATHY_CONTACT_LIST_STORE_COL_STATUS,
						      GTK_SORT_ASCENDING);
		break;

	case EMPATHY_CONTACT_LIST_STORE_SORT_NAME:
		gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
						      EMPATHY_CONTACT_LIST_STORE_COL_NAME,
						      GTK_SORT_ASCENDING);
		break;

	default:
		g_assert_not_reached ();
	}

	g_object_notify (G_OBJECT (store), "sort-criterium");
}

gboolean
empathy_contact_list_store_row_separator_func (GtkTreeModel *model,
					      GtkTreeIter  *iter,
					      gpointer      data)
{
	gboolean is_separator = FALSE;

	g_return_val_if_fail (GTK_IS_TREE_MODEL (model), FALSE);

	gtk_tree_model_get (model, iter,
			    EMPATHY_CONTACT_LIST_STORE_COL_IS_SEPARATOR, &is_separator,
			    -1);

	return is_separator;
}

gchar *
empathy_contact_list_store_get_parent_group (GtkTreeModel *model,
					    GtkTreePath  *path,
					    gboolean     *path_is_group,
					    gboolean     *is_fake_group)
{
	GtkTreeIter  parent_iter, iter;
	gchar       *name = NULL;
	gboolean     is_group;
	gboolean     fake;

	g_return_val_if_fail (GTK_IS_TREE_MODEL (model), NULL);

	if (path_is_group) {
		*path_is_group = FALSE;
	}

	if (!gtk_tree_model_get_iter (model, &iter, path)) {
		return NULL;
	}

	gtk_tree_model_get (model, &iter,
			    EMPATHY_CONTACT_LIST_STORE_COL_IS_GROUP, &is_group,
			    EMPATHY_CONTACT_LIST_STORE_COL_NAME, &name,
			    -1);

	if (!is_group) {
		g_free (name);
		name = NULL;

		if (!gtk_tree_model_iter_parent (model, &parent_iter, &iter)) {
			return NULL;
		}

		iter = parent_iter;

		gtk_tree_model_get (model, &iter,
				    EMPATHY_CONTACT_LIST_STORE_COL_IS_GROUP, &is_group,
				    EMPATHY_CONTACT_LIST_STORE_COL_NAME, &name,
				    EMPATHY_CONTACT_LIST_STORE_COL_IS_FAKE_GROUP, &fake,
				    -1);
		if (!is_group) {
			g_free (name);
			return NULL;
		}
	}

	if (path_is_group) {
		*path_is_group = TRUE;
	}

	if (is_fake_group != NULL)
		*is_fake_group = fake;

	return name;
}

static void
contact_list_store_setup (EmpathyContactListStore *store)
{
	EmpathyContactListStorePriv *priv;
	GType types[] = {
		GDK_TYPE_PIXBUF,      /* Status pixbuf */
		GDK_TYPE_PIXBUF,      /* Avatar pixbuf */
		G_TYPE_BOOLEAN,       /* Avatar pixbuf visible */
		G_TYPE_STRING,        /* Name */
		G_TYPE_UINT,          /* Presence type */
		G_TYPE_STRING,        /* Status string */
		G_TYPE_BOOLEAN,       /* Compact view */
		EMPATHY_TYPE_CONTACT, /* Contact type */
		G_TYPE_BOOLEAN,       /* Is group */
		G_TYPE_BOOLEAN,       /* Is active */
		G_TYPE_BOOLEAN,       /* Is online */
		G_TYPE_BOOLEAN,       /* Is separator */
		G_TYPE_BOOLEAN,       /* Can make audio calls */
		G_TYPE_BOOLEAN,       /* Can make video calls */
		EMPATHY_TYPE_CONTACT_LIST_FLAGS, /* Flags */
		G_TYPE_BOOLEAN,       /* Is a fake group */
	};

	priv = GET_PRIV (store);

	gtk_tree_store_set_column_types (GTK_TREE_STORE (store),
					 EMPATHY_CONTACT_LIST_STORE_COL_COUNT,
					 types);

	/* Set up sorting */
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (store),
					 EMPATHY_CONTACT_LIST_STORE_COL_NAME,
					 contact_list_store_name_sort_func,
					 store, NULL);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (store),
					 EMPATHY_CONTACT_LIST_STORE_COL_STATUS,
					 contact_list_store_state_sort_func,
					 store, NULL);

	priv->sort_criterium = EMPATHY_CONTACT_LIST_STORE_SORT_NAME;
	empathy_contact_list_store_set_sort_criterium (store, priv->sort_criterium);
}

static gboolean
contact_list_store_inibit_active_cb (EmpathyContactListStore *store)
{
	EmpathyContactListStorePriv *priv;

	priv = GET_PRIV (store);

	priv->show_active = TRUE;
	priv->inhibit_active = 0;

	return FALSE;
}

static void
contact_list_store_add_contact_and_connect (EmpathyContactListStore *store, EmpathyContact *contact)
{
	g_signal_connect (contact, "notify::presence",
			  G_CALLBACK (contact_list_store_contact_updated_cb),
			  store);
	g_signal_connect (contact, "notify::presence-message",
			  G_CALLBACK (contact_list_store_contact_updated_cb),
			  store);
	g_signal_connect (contact, "notify::name",
			  G_CALLBACK (contact_list_store_contact_updated_cb),
			  store);
	g_signal_connect (contact, "notify::avatar",
			  G_CALLBACK (contact_list_store_contact_updated_cb),
			  store);
	g_signal_connect (contact, "notify::capabilities",
			  G_CALLBACK (contact_list_store_contact_updated_cb),
			  store);

	contact_list_store_add_contact (store, contact);
}

static void
contact_list_store_remove_contact_and_disconnect (EmpathyContactListStore *store, EmpathyContact *contact)
{
	g_signal_handlers_disconnect_by_func (contact,
					      G_CALLBACK (contact_list_store_contact_updated_cb),
					      store);

	contact_list_store_remove_contact (store, contact);
}

static void
contact_list_store_members_changed_cb (EmpathyContactList      *list_iface,
				       EmpathyContact          *contact,
				       EmpathyContact          *actor,
				       guint                    reason,
				       gchar                   *message,
				       gboolean                 is_member,
				       EmpathyContactListStore *store)
{
	DEBUG ("Contact %s (%d) %s",
		empathy_contact_get_id (contact),
		empathy_contact_get_handle (contact),
		is_member ? "added" : "removed");

	if (is_member) {
		contact_list_store_add_contact_and_connect (store, contact);
	} else {
		contact_list_store_remove_contact_and_disconnect (store, contact);
	}
}

static void
contact_list_store_member_renamed_cb (EmpathyContactList      *list_iface,
				      EmpathyContact          *old_contact,
				      EmpathyContact          *new_contact,
				      guint                    reason,
				      gchar                   *message,
				      EmpathyContactListStore *store)
{
	DEBUG ("Contact %s (%d) renamed to %s (%d)",
		empathy_contact_get_id (old_contact),
		empathy_contact_get_handle (old_contact),
		empathy_contact_get_id (new_contact),
		empathy_contact_get_handle (new_contact));

	/* add the new contact */
	contact_list_store_add_contact_and_connect (store, new_contact);

	/* remove old contact */
	contact_list_store_remove_contact_and_disconnect (store, old_contact);
}

static void
contact_list_store_groups_changed_cb (EmpathyContactList      *list_iface,
				      EmpathyContact          *contact,
				      gchar                   *group,
				      gboolean                 is_member,
				      EmpathyContactListStore *store)
{
	EmpathyContactListStorePriv *priv;
	gboolean                     show_active;

	priv = GET_PRIV (store);

	DEBUG ("Updating groups for contact %s (%d)",
		empathy_contact_get_id (contact),
		empathy_contact_get_handle (contact));

	/* We do this to make sure the groups are correct, if not, we
	 * would have to check the groups already set up for each
	 * contact and then see what has been updated.
	 */
	show_active = priv->show_active;
	priv->show_active = FALSE;
	contact_list_store_remove_contact (store, contact);
	contact_list_store_add_contact (store, contact);
	priv->show_active = show_active;
}

static void
add_contact_to_store (GtkTreeStore *store,
		      GtkTreeIter *iter,
		      GtkTreeIter *parent,
		      EmpathyContact *contact,
		      EmpathyContactListFlags flags)
{
	EmpathyContactListStorePriv *priv = GET_PRIV (store);
	GQueue *queue;

	gtk_tree_store_insert_with_values (store, iter, parent, 0,
			    EMPATHY_CONTACT_LIST_STORE_COL_NAME, empathy_contact_get_alias (contact),
			    EMPATHY_CONTACT_LIST_STORE_COL_CONTACT, contact,
			    EMPATHY_CONTACT_LIST_STORE_COL_IS_GROUP, FALSE,
			    EMPATHY_CONTACT_LIST_STORE_COL_IS_SEPARATOR, FALSE,
			    EMPATHY_CONTACT_LIST_STORE_COL_CAN_AUDIO_CALL,
			      empathy_contact_get_capabilities (contact) &
			        EMPATHY_CAPABILITIES_AUDIO,
			    EMPATHY_CONTACT_LIST_STORE_COL_CAN_VIDEO_CALL,
			      empathy_contact_get_capabilities (contact) &
			        EMPATHY_CAPABILITIES_VIDEO,
			    EMPATHY_CONTACT_LIST_STORE_COL_FLAGS, flags,
			    -1);

	queue = g_hash_table_lookup (priv->empathy_contact_cache, contact);
	if (queue) {
		g_queue_push_tail (queue, gtk_tree_iter_copy (iter));
	} else {
		queue = g_queue_new ();
		g_queue_push_tail (queue, gtk_tree_iter_copy (iter));
		g_hash_table_insert (priv->empathy_contact_cache, contact,
			queue);
	}
}

static void
contact_list_store_add_contact (EmpathyContactListStore *store,
				EmpathyContact          *contact)
{
	EmpathyContactListStorePriv *priv;
	GtkTreeIter                 iter;
	GList                      *groups = NULL, *l;
	TpConnection               *connection;
	EmpathyContactListFlags     flags = 0;
	char                       *protocol_name;

	priv = GET_PRIV (store);

	if (EMP_STR_EMPTY (empathy_contact_get_alias (contact)) ||
	    (!priv->show_offline && !empathy_contact_is_online (contact))) {
		return;
	}

	if (priv->show_groups) {
		groups = empathy_contact_list_get_groups (priv->list, contact);
	}

	connection = empathy_contact_get_connection (contact);
	if (EMPATHY_IS_CONTACT_MANAGER (priv->list)) {
		flags = empathy_contact_manager_get_flags_for_connection (
			EMPATHY_CONTACT_MANAGER (priv->list), connection);
	}

	tp_connection_parse_object_path (connection, &protocol_name, NULL);

	if (!groups) {
		GtkTreeIter iter_group, *parent;

		parent = &iter_group;

		if (!priv->show_groups) {
			parent = NULL;
		}

		add_contact_to_store (GTK_TREE_STORE (store), &iter, parent,
				      contact, flags);
	}

	g_free (protocol_name);

	/* Else add to each group. */
	for (l = groups; l; l = l->next) {
		GtkTreeIter iter_group;

		contact_list_store_get_group (store, l->data, &iter_group, NULL, NULL, FALSE);

		add_contact_to_store (GTK_TREE_STORE (store), &iter, &iter_group, contact, flags);
		g_free (l->data);
	}
	g_list_free (groups);

	contact_list_store_contact_update (store, contact);
}

static void
contact_list_store_remove_contact (EmpathyContactListStore *store,
				   EmpathyContact          *contact)
{
	EmpathyContactListStorePriv *priv = GET_PRIV (store);
	GtkTreeModel               *model;
	GList                      *l;
	GQueue                     *row_refs;

	row_refs = g_hash_table_lookup (priv->empathy_contact_cache, contact);
	if (!row_refs) {
		return;
	}

	/* Clean up model */
	model = GTK_TREE_MODEL (store);

	for (l = g_queue_peek_head_link (row_refs); l; l = l->next) {
		GtkTreeIter *iter = l->data;
		GtkTreeIter parent;

		/* NOTE: it is only <= 2 here because we have
		 * separators after the group name, otherwise it
		 * should be 1.
		 */
		if (gtk_tree_model_iter_parent (model, &parent, iter) &&
		    gtk_tree_model_iter_n_children (model, &parent) <= 2) {
			gchar *group_name;
			gtk_tree_model_get (model, &parent,
			    EMPATHY_CONTACT_LIST_STORE_COL_NAME, &group_name,
			    -1);
			g_hash_table_remove (priv->empathy_group_cache,
				group_name);
			gtk_tree_store_remove (GTK_TREE_STORE (store), &parent);
		} else {
			gtk_tree_store_remove (GTK_TREE_STORE (store), iter);
		}
	}

	g_hash_table_remove (priv->empathy_contact_cache, contact);
}

static void
contact_list_store_contact_update (EmpathyContactListStore *store,
				   EmpathyContact          *contact)
{
	EmpathyContactListStorePriv *priv;
	ShowActiveData             *data;
	GtkTreeModel               *model;
	GList                      *iters, *l;
	gboolean                    in_list;
	gboolean                    should_be_in_list;
	gboolean                    was_online = TRUE;
	gboolean                    now_online = FALSE;
	gboolean                    set_model = FALSE;
	gboolean                    do_remove = FALSE;
	gboolean                    do_set_active = FALSE;
	gboolean                    do_set_refresh = FALSE;
	gboolean                    show_avatar = FALSE;
	GdkPixbuf                  *pixbuf_avatar;
	GdkPixbuf                  *pixbuf_status;

	priv = GET_PRIV (store);

	model = GTK_TREE_MODEL (store);

	iters = contact_list_store_find_contact (store, contact);
	if (!iters) {
		in_list = FALSE;
	} else {
		in_list = TRUE;
	}

	/* Get online state now. */
	now_online = empathy_contact_is_online (contact);

	if (priv->show_offline || now_online) {
		should_be_in_list = TRUE;
	} else {
		should_be_in_list = FALSE;
	}

	if (!in_list && !should_be_in_list) {
		/* Nothing to do. */
		DEBUG ("Contact:'%s' in list:NO, should be:NO",
			empathy_contact_get_alias (contact));

		g_list_foreach (iters, (GFunc) gtk_tree_iter_free, NULL);
		g_list_free (iters);
		return;
	}
	else if (in_list && !should_be_in_list) {
		DEBUG ("Contact:'%s' in list:YES, should be:NO",
			empathy_contact_get_alias (contact));

		if (priv->show_active) {
			do_remove = TRUE;
			do_set_active = TRUE;
			do_set_refresh = TRUE;

			set_model = TRUE;
			DEBUG ("Remove item (after timeout)");
		} else {
			DEBUG ("Remove item (now)!");
			contact_list_store_remove_contact (store, contact);
		}
	}
	else if (!in_list && should_be_in_list) {
		DEBUG ("Contact:'%s' in list:NO, should be:YES",
			empathy_contact_get_alias (contact));

		contact_list_store_add_contact (store, contact);

		if (priv->show_active) {
			do_set_active = TRUE;

			DEBUG ("Set active (contact added)");
		}
	} else {
		DEBUG ("Contact:'%s' in list:YES, should be:YES",
			empathy_contact_get_alias (contact));

		/* Get online state before. */
		if (iters && g_list_length (iters) > 0) {
			gtk_tree_model_get (model, iters->data,
					    EMPATHY_CONTACT_LIST_STORE_COL_IS_ONLINE, &was_online,
					    -1);
		}

		/* Is this really an update or an online/offline. */
		if (priv->show_active) {
			if (was_online != now_online) {
				do_set_active = TRUE;
				do_set_refresh = TRUE;

				DEBUG ("Set active (contact updated %s)",
					was_online ? "online  -> offline" :
					"offline -> online");
			} else {
				/* Was TRUE for presence updates. */
				/* do_set_active = FALSE;  */
				do_set_refresh = TRUE;

				DEBUG ("Set active (contact updated)");
			}
		}

		set_model = TRUE;
	}

	if (priv->show_avatars && !priv->is_compact) {
		show_avatar = TRUE;
	}
	pixbuf_avatar = empathy_pixbuf_avatar_from_contact_scaled (contact, 32, 32);
	pixbuf_status = contact_list_store_get_contact_status_icon (store, contact);
	for (l = iters; l && set_model; l = l->next) {
		gtk_tree_store_set (GTK_TREE_STORE (store), l->data,
				    EMPATHY_CONTACT_LIST_STORE_COL_ICON_STATUS, pixbuf_status,
				    EMPATHY_CONTACT_LIST_STORE_COL_PIXBUF_AVATAR, pixbuf_avatar,
				    EMPATHY_CONTACT_LIST_STORE_COL_PIXBUF_AVATAR_VISIBLE, show_avatar,
				    EMPATHY_CONTACT_LIST_STORE_COL_NAME, empathy_contact_get_alias (contact),
				    EMPATHY_CONTACT_LIST_STORE_COL_PRESENCE_TYPE,
				      empathy_contact_get_presence (contact),
				    EMPATHY_CONTACT_LIST_STORE_COL_STATUS,
				      empathy_contact_get_presence_message (contact),
				    EMPATHY_CONTACT_LIST_STORE_COL_COMPACT, priv->is_compact,
				    EMPATHY_CONTACT_LIST_STORE_COL_IS_GROUP, FALSE,
				    EMPATHY_CONTACT_LIST_STORE_COL_IS_ONLINE, now_online,
				    EMPATHY_CONTACT_LIST_STORE_COL_IS_SEPARATOR, FALSE,
				    EMPATHY_CONTACT_LIST_STORE_COL_CAN_AUDIO_CALL,
				      empathy_contact_get_capabilities (contact) &
				        EMPATHY_CAPABILITIES_AUDIO,
				    EMPATHY_CONTACT_LIST_STORE_COL_CAN_VIDEO_CALL,
				      empathy_contact_get_capabilities (contact) &
				        EMPATHY_CAPABILITIES_VIDEO,
				    -1);
	}

	if (pixbuf_avatar) {
		g_object_unref (pixbuf_avatar);
	}

	if (priv->show_active && do_set_active) {
		contact_list_store_contact_set_active (store, contact, do_set_active, do_set_refresh);

		if (do_set_active) {
			data = contact_list_store_contact_active_new (store, contact, do_remove);
			g_timeout_add_seconds (ACTIVE_USER_SHOW_TIME,
					       (GSourceFunc) contact_list_store_contact_active_cb,
					       data);
		}
	}

	/* FIXME: when someone goes online then offline quickly, the
	 * first timeout sets the user to be inactive and the second
	 * timeout removes the user from the contact list, really we
	 * should remove the first timeout.
	 */
	g_list_foreach (iters, (GFunc) gtk_tree_iter_free, NULL);
	g_list_free (iters);
}

static void
contact_list_store_contact_updated_cb (EmpathyContact          *contact,
				       GParamSpec              *param,
				       EmpathyContactListStore *store)
{
	DEBUG ("Contact:'%s' updated, checking roster is in sync...",
		empathy_contact_get_alias (contact));

	contact_list_store_contact_update (store, contact);
}

static void
contact_list_store_contact_set_active (EmpathyContactListStore *store,
				       EmpathyContact          *contact,
				       gboolean                active,
				       gboolean                set_changed)
{
	GtkTreeModel               *model;
	GList                      *iters, *l;

	model = GTK_TREE_MODEL (store);

	iters = contact_list_store_find_contact (store, contact);
	for (l = iters; l; l = l->next) {
		GtkTreePath *path;

		gtk_tree_store_set (GTK_TREE_STORE (store), l->data,
				    EMPATHY_CONTACT_LIST_STORE_COL_IS_ACTIVE, active,
				    -1);

		DEBUG ("Set item %s", active ? "active" : "inactive");

		if (set_changed) {
			path = gtk_tree_model_get_path (model, l->data);
			gtk_tree_model_row_changed (model, path, l->data);
			gtk_tree_path_free (path);
		}
	}

	g_list_foreach (iters, (GFunc) gtk_tree_iter_free, NULL);
	g_list_free (iters);

}

static ShowActiveData *
contact_list_store_contact_active_new (EmpathyContactListStore *store,
				       EmpathyContact          *contact,
				       gboolean                remove_)
{
	ShowActiveData *data;

	DEBUG ("Contact:'%s' now active, and %s be removed",
		empathy_contact_get_alias (contact),
		remove_ ? "WILL" : "WILL NOT");

	data = g_slice_new0 (ShowActiveData);

	data->store = g_object_ref (store);
	data->contact = g_object_ref (contact);
	data->remove = remove_;

	return data;
}

static void
contact_list_store_contact_active_free (ShowActiveData *data)
{
	g_object_unref (data->contact);
	g_object_unref (data->store);

	g_slice_free (ShowActiveData, data);
}

static gboolean
contact_list_store_contact_active_cb (ShowActiveData *data)
{
	EmpathyContactListStorePriv *priv;

	priv = GET_PRIV (data->store);

	if (data->remove &&
	    !priv->show_offline &&
	    !empathy_contact_is_online (data->contact)) {
		DEBUG ("Contact:'%s' active timeout, removing item",
			empathy_contact_get_alias (data->contact));
		contact_list_store_remove_contact (data->store, data->contact);
	}

	DEBUG ("Contact:'%s' no longer active",
		empathy_contact_get_alias (data->contact));

	contact_list_store_contact_set_active (data->store,
					       data->contact,
					       FALSE,
					       TRUE);

	contact_list_store_contact_active_free (data);

	return FALSE;
}

static void
contact_list_store_get_group (EmpathyContactListStore *store,
			      const gchar            *name,
			      GtkTreeIter            *iter_group_to_set,
			      GtkTreeIter            *iter_separator_to_set,
			      gboolean               *created,
			      gboolean               is_fake_group)
{
	EmpathyContactListStorePriv *priv = GET_PRIV (store);
	GtkTreeModel                *model;
	GtkTreeIter                  iter_group;
	GtkTreeIter                  iter_separator;
	GtkTreeIter                 *iter;

	model = GTK_TREE_MODEL (store);
	iter = g_hash_table_lookup (priv->empathy_group_cache, name);

	if (iter == NULL) {
		if (created) {
			*created = TRUE;
		}

		gtk_tree_store_insert_with_values (GTK_TREE_STORE (store), &iter_group, NULL, 0,
				    EMPATHY_CONTACT_LIST_STORE_COL_ICON_STATUS, NULL,
				    EMPATHY_CONTACT_LIST_STORE_COL_NAME, name,
				    EMPATHY_CONTACT_LIST_STORE_COL_IS_GROUP, TRUE,
				    EMPATHY_CONTACT_LIST_STORE_COL_IS_ACTIVE, FALSE,
				    EMPATHY_CONTACT_LIST_STORE_COL_IS_SEPARATOR, FALSE,
				    EMPATHY_CONTACT_LIST_STORE_COL_IS_FAKE_GROUP, is_fake_group,
				    -1);

		g_hash_table_insert (priv->empathy_group_cache,
			g_strdup (name), gtk_tree_iter_copy (&iter_group));

		if (iter_group_to_set) {
			*iter_group_to_set = iter_group;
		}

		gtk_tree_store_insert_with_values (GTK_TREE_STORE (store), &iter_separator, &iter_group, 0,
				    EMPATHY_CONTACT_LIST_STORE_COL_IS_SEPARATOR, TRUE,
				    -1);

		if (iter_separator_to_set) {
			*iter_separator_to_set = iter_separator;
		}
	} else {
		if (created) {
			*created = FALSE;
		}

		if (iter_group_to_set) {
			*iter_group_to_set = *iter;
		}

		iter_separator = *iter;

		if (gtk_tree_model_iter_next (model, &iter_separator)) {
			gboolean is_separator;

			gtk_tree_model_get (model, &iter_separator,
					    EMPATHY_CONTACT_LIST_STORE_COL_IS_SEPARATOR, &is_separator,
					    -1);

			if (is_separator && iter_separator_to_set) {
				*iter_separator_to_set = iter_separator;
			}
		}
	}
}

static gint
compare_separator_and_groups (gboolean is_separator_a,
			      gboolean is_separator_b,
			      const gchar *name_a,
			      const gchar *name_b,
			      EmpathyContact *contact_a,
			      EmpathyContact *contact_b,
			      gboolean fake_group_a,
			      gboolean fake_group_b)
{
	if (is_separator_a || is_separator_b) {
		/* We have at least one separator */
		if (is_separator_a) {
			return -1;
		} else if (is_separator_b) {
			return 1;
		}
	}

	/* One group and one contact */
	if (!contact_a && contact_b) {
		return 1;
	} else if (contact_a && !contact_b) {
		return -1;
	} else if (!contact_a && !contact_b) {
		return g_utf8_collate (name_a, name_b);
	}

	/* Two contacts, ordering depends of the sorting policy */
	return 0;
}

static gint
contact_list_store_contact_sort (EmpathyContact *contact_a,
				 EmpathyContact *contact_b)
{
	TpAccount *account_a, *account_b;
	gint ret_val;

	g_return_val_if_fail (contact_a != NULL || contact_b != NULL, 0);

	/* alias */
	ret_val = g_utf8_collate (empathy_contact_get_alias (contact_a),
				  empathy_contact_get_alias (contact_b));

	if (ret_val != 0)
		goto out;

	/* identifier */
	ret_val = g_utf8_collate (empathy_contact_get_id (contact_a),
				  empathy_contact_get_id (contact_b));

	if (ret_val != 0)
		goto out;

	account_a = empathy_contact_get_account (contact_a);
	account_b = empathy_contact_get_account (contact_b);

	/* protocol */
	ret_val = strcmp (tp_account_get_protocol (account_a),
			  tp_account_get_protocol (account_b));

	if (ret_val != 0)
		goto out;

	/* account ID */
	ret_val = strcmp (tp_proxy_get_object_path (account_a),
			  tp_proxy_get_object_path (account_b));

out:
	return ret_val;
}

static gint
contact_list_store_state_sort_func (GtkTreeModel *model,
				    GtkTreeIter  *iter_a,
				    GtkTreeIter  *iter_b,
				    gpointer      user_data)
{
	gint            ret_val;
	gchar          *name_a, *name_b;
	gboolean        is_separator_a, is_separator_b;
	EmpathyContact *contact_a, *contact_b;
	gboolean       fake_group_a, fake_group_b;

	gtk_tree_model_get (model, iter_a,
			    EMPATHY_CONTACT_LIST_STORE_COL_NAME, &name_a,
			    EMPATHY_CONTACT_LIST_STORE_COL_CONTACT, &contact_a,
			    EMPATHY_CONTACT_LIST_STORE_COL_IS_SEPARATOR, &is_separator_a,
			    EMPATHY_CONTACT_LIST_STORE_COL_IS_FAKE_GROUP, &fake_group_a,
			    -1);
	gtk_tree_model_get (model, iter_b,
			    EMPATHY_CONTACT_LIST_STORE_COL_NAME, &name_b,
			    EMPATHY_CONTACT_LIST_STORE_COL_CONTACT, &contact_b,
			    EMPATHY_CONTACT_LIST_STORE_COL_IS_SEPARATOR, &is_separator_b,
			    EMPATHY_CONTACT_LIST_STORE_COL_IS_FAKE_GROUP, &fake_group_b,
			    -1);

	if (contact_a == NULL || contact_b == NULL) {
		ret_val = compare_separator_and_groups (is_separator_a, is_separator_b,
			name_a, name_b, contact_a, contact_b, fake_group_a, fake_group_b);
		goto free_and_out;
	}

	/* If we managed to get this far, we can start looking at
	 * the presences.
	 */
	ret_val = -tp_connection_presence_type_cmp_availability (
		empathy_contact_get_presence (EMPATHY_CONTACT (contact_a)),
		empathy_contact_get_presence (EMPATHY_CONTACT (contact_b)));

	if (ret_val == 0) {
		/* Fallback: compare by name et al. */
		ret_val = contact_list_store_contact_sort (contact_a, contact_b);
	}

free_and_out:
	g_free (name_a);
	g_free (name_b);

	if (contact_a) {
		g_object_unref (contact_a);
	}

	if (contact_b) {
		g_object_unref (contact_b);
	}

	return ret_val;
}

static gint
contact_list_store_name_sort_func (GtkTreeModel *model,
				   GtkTreeIter  *iter_a,
				   GtkTreeIter  *iter_b,
				   gpointer      user_data)
{
	gchar         *name_a, *name_b;
	EmpathyContact *contact_a, *contact_b;
	gboolean       is_separator_a = FALSE, is_separator_b = FALSE;
	gint           ret_val;
	gboolean       fake_group_a, fake_group_b;

	gtk_tree_model_get (model, iter_a,
			    EMPATHY_CONTACT_LIST_STORE_COL_NAME, &name_a,
			    EMPATHY_CONTACT_LIST_STORE_COL_CONTACT, &contact_a,
			    EMPATHY_CONTACT_LIST_STORE_COL_IS_SEPARATOR, &is_separator_a,
			    EMPATHY_CONTACT_LIST_STORE_COL_IS_FAKE_GROUP, &fake_group_a,
			    -1);
	gtk_tree_model_get (model, iter_b,
			    EMPATHY_CONTACT_LIST_STORE_COL_NAME, &name_b,
			    EMPATHY_CONTACT_LIST_STORE_COL_CONTACT, &contact_b,
			    EMPATHY_CONTACT_LIST_STORE_COL_IS_SEPARATOR, &is_separator_b,
			    EMPATHY_CONTACT_LIST_STORE_COL_IS_FAKE_GROUP, &fake_group_b,
			    -1);

	if (contact_a == NULL || contact_b == NULL)
		ret_val = compare_separator_and_groups (is_separator_a, is_separator_b,
			name_a, name_b, contact_a, contact_b, fake_group_a, fake_group_b);
	else
		ret_val = contact_list_store_contact_sort (contact_a, contact_b);

	if (contact_a) {
		g_object_unref (contact_a);
	}

	if (contact_b) {
		g_object_unref (contact_b);
	}

	return ret_val;
}

static GList *
contact_list_store_find_contact (EmpathyContactListStore *store,
				 EmpathyContact          *contact)
{
	EmpathyContactListStorePriv *priv = GET_PRIV (store);
	GQueue                    *row_refs_queue;
	GList                     *i;
	GList                     *iters_list = NULL;

	row_refs_queue = g_hash_table_lookup (priv->empathy_contact_cache, contact);
	if (!row_refs_queue)
		return NULL;

	for (i = g_queue_peek_head_link (row_refs_queue) ; i != NULL ;
	     i = i->next) {
		GtkTreeIter *iter = i->data;

		iters_list = g_list_prepend
			(iters_list, gtk_tree_iter_copy (iter));
	}

	return iters_list;
}

static gboolean
contact_list_store_update_list_mode_foreach (GtkTreeModel           *model,
					     GtkTreePath            *path,
					     GtkTreeIter            *iter,
					     EmpathyContactListStore *store)
{
	EmpathyContactListStorePriv *priv;
	gboolean                     show_avatar = FALSE;
	EmpathyContact              *contact;
	GdkPixbuf                   *pixbuf_status;

	priv = GET_PRIV (store);

	if (priv->show_avatars && !priv->is_compact) {
		show_avatar = TRUE;
	}

	gtk_tree_model_get (model, iter,
			    EMPATHY_CONTACT_LIST_STORE_COL_CONTACT, &contact,
			    -1);

	if (contact == NULL){
		return FALSE;
	}
	/* get icon from hash_table */
	pixbuf_status = contact_list_store_get_contact_status_icon (store, contact);

	gtk_tree_store_set (GTK_TREE_STORE (store), iter,
			    EMPATHY_CONTACT_LIST_STORE_COL_ICON_STATUS, pixbuf_status,
			    EMPATHY_CONTACT_LIST_STORE_COL_PIXBUF_AVATAR_VISIBLE, show_avatar,
			    EMPATHY_CONTACT_LIST_STORE_COL_COMPACT, priv->is_compact,
			    -1);

	return FALSE;
}

static GdkPixbuf *
contact_list_store_get_contact_status_icon_with_icon_name (
					EmpathyContactListStore *store,
					EmpathyContact *contact,
					const gchar *status_icon_name)
{
	GdkPixbuf                   *pixbuf_status = NULL;
	EmpathyContactListStorePriv *priv;
	const gchar                 *protocol_name = NULL;
	gchar                       *icon_name = NULL;

	priv = GET_PRIV (store);

	if (priv->show_protocols) {
		protocol_name = empathy_protocol_name_for_contact (contact);
		icon_name = g_strdup_printf ("%s-%s", status_icon_name, protocol_name);
	} else {
		icon_name = g_strdup_printf ("%s", status_icon_name);
	}
	pixbuf_status = g_hash_table_lookup (priv->status_icons, icon_name);
	if (pixbuf_status == NULL) {
		pixbuf_status = empathy_pixbuf_contact_status_icon_with_icon_name (contact,
				    status_icon_name,
				    priv->show_protocols);
		if (pixbuf_status != NULL) {
			g_hash_table_insert (priv->status_icons,
			    g_strdup (icon_name),
			    pixbuf_status);
		}
	}

	g_free (icon_name);
	return pixbuf_status;
}

GdkPixbuf *
contact_list_store_get_contact_status_icon (EmpathyContactListStore *store,
					    EmpathyContact *contact)
{
	GdkPixbuf                   *pixbuf_status = NULL;
	EmpathyContactListStorePriv *priv;
	const gchar                 *status_icon_name = NULL;
	gboolean                     composing = FALSE;

	priv = GET_PRIV (store);

	if (EMPATHY_IS_TP_CHAT (priv->list)) {
		if (empathy_tp_chat_get_chat_state (EMPATHY_TP_CHAT (priv->list),
		      contact) ==
			TP_CHANNEL_CHAT_STATE_COMPOSING)
		composing = TRUE;
	}

	if (composing) {
		status_icon_name = EMPATHY_IMAGE_TYPING;
	} else {
		status_icon_name = empathy_icon_name_for_contact (contact);
	}

	if (status_icon_name == NULL)
		return NULL;

	pixbuf_status = contact_list_store_get_contact_status_icon_with_icon_name (
			    store,
			    contact,
			    status_icon_name);

	return pixbuf_status;
}
