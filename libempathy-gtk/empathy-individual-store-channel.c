/*
 * Copyright (C) 2005-2007 Imendio AB
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
 * Authors: Mikael Hallendal <micke@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 *          Travis Reitter <travis.reitter@collabora.co.uk>
 */

#include "config.h"

#include <string.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <folks/folks.h>
#include <folks/folks-telepathy.h>
#include <telepathy-glib/util.h>

#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-enum-types.h>

#include "empathy-individual-store-channel.h"

#include "empathy-ui-utils.h"
#include "empathy-gtk-enum-types.h"

#define DEBUG_FLAG EMPATHY_DEBUG_CONTACT
#include <libempathy/empathy-debug.h>

struct _EmpathyIndividualStoreChannelPriv
{
  TpChannel *channel;

  /* TpContact => FolksIndividual
   * We keep the individuals we have added to the store so can easily remove
   * them when their TpContact leaves the channel. */
  GHashTable *individuals;
};

enum
{
  PROP_0,
  PROP_CHANNEL,
};


G_DEFINE_TYPE (EmpathyIndividualStoreChannel, empathy_individual_store_channel,
    EMPATHY_TYPE_INDIVIDUAL_STORE);

static void
add_members (EmpathyIndividualStoreChannel *self,
    GPtrArray *members)
{
  EmpathyIndividualStore *store = (EmpathyIndividualStore *) self;
  guint i;

  for (i = 0; i < members->len; i++)
    {
      TpContact *contact = g_ptr_array_index (members, i);
      FolksIndividual *individual;

      if (g_hash_table_lookup (self->priv->individuals, contact) != NULL)
        continue;

      individual = empathy_create_individual_from_tp_contact (contact);

      DEBUG ("%s joined channel %s", tp_contact_get_identifier (contact),
          tp_proxy_get_object_path (self->priv->channel));

      individual_store_add_individual_and_connect (store, individual);

      /* Pass the individual reference to the hash table */
      g_hash_table_insert (self->priv->individuals, g_object_ref (contact),
          individual);
    }
}

static void
remove_members (EmpathyIndividualStoreChannel *self,
    GPtrArray *members)
{
  EmpathyIndividualStore *store = (EmpathyIndividualStore *) self;
  guint i;

  for (i = 0; i < members->len; i++)
    {
      TpContact *contact = g_ptr_array_index (members, i);
      FolksIndividual *individual;

      individual = g_hash_table_lookup (self->priv->individuals, contact);
      if (individual == NULL)
        continue;

      DEBUG ("%s left channel %s", tp_contact_get_identifier (contact),
          tp_proxy_get_object_path (self->priv->channel));

      individual_store_remove_individual_and_disconnect (store, individual);

      g_hash_table_remove (self->priv->individuals, contact);
    }
}

static void
group_contacts_changed_cb (TpChannel *channel,
    GPtrArray *added,
    GPtrArray *removed,
    GPtrArray *local_pending,
    GPtrArray *remote_pending,
    TpContact *actor,
    GHashTable *details,
    gpointer user_data)
{
  EmpathyIndividualStoreChannel *self = EMPATHY_INDIVIDUAL_STORE_CHANNEL (
      user_data);

  remove_members (self, removed);
  add_members (self, added);
}

static void
channel_prepare_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyIndividualStoreChannel *self = user_data;
  TpChannel *channel = (TpChannel *) source;
  GError *error = NULL;
  GPtrArray *members;

  if (!tp_proxy_prepare_finish (source, result, &error))
    {
      DEBUG ("Failed to prepare %s: %s", tp_proxy_get_object_path (source),
          error->message);

      g_error_free (error);
    }

  /* Add initial members */
  members = tp_channel_group_dup_members_contacts (channel);
  if (members != NULL)
    {
      add_members (self, members);
      g_ptr_array_unref (members);
    }

  tp_g_signal_connect_object (channel, "group-contacts-changed",
      G_CALLBACK (group_contacts_changed_cb), self, 0);
}

static void
individual_store_channel_set_individual_channel (
    EmpathyIndividualStoreChannel *self,
    TpChannel *channel)
{
  GQuark features[] = { TP_CHANNEL_FEATURE_CONTACTS, 0 };

  g_assert (self->priv->channel == NULL); /* construct only */
  self->priv->channel = g_object_ref (channel);

  tp_proxy_prepare_async (channel, features, channel_prepare_cb, self);
}

static void
individual_store_channel_dispose (GObject *object)
{
  EmpathyIndividualStoreChannel *self = EMPATHY_INDIVIDUAL_STORE_CHANNEL (
      object);
  EmpathyIndividualStore *store = EMPATHY_INDIVIDUAL_STORE (object);
  GHashTableIter iter;
  gpointer v;

  g_hash_table_iter_init (&iter, self->priv->individuals);
  while (g_hash_table_iter_next (&iter, NULL, &v))
    {
      FolksIndividual *individual = v;

      empathy_individual_store_disconnect_individual (store, individual);
    }

  tp_clear_pointer (&self->priv->individuals, g_hash_table_unref);
  g_clear_object (&self->priv->channel);

  G_OBJECT_CLASS (empathy_individual_store_channel_parent_class)->dispose (
      object);
}

static void
individual_store_channel_get_property (GObject *object,
    guint param_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyIndividualStoreChannel *self = EMPATHY_INDIVIDUAL_STORE_CHANNEL (
      object);

  switch (param_id)
    {
    case PROP_CHANNEL:
      g_value_set_object (value, self->priv->channel);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    };
}

static void
individual_store_channel_set_property (GObject *object,
    guint param_id,
    const GValue *value,
    GParamSpec *pspec)
{
  switch (param_id)
    {
    case PROP_CHANNEL:
      individual_store_channel_set_individual_channel (
          EMPATHY_INDIVIDUAL_STORE_CHANNEL (object),
          g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    };
}

static void
individual_store_channel_reload_individuals (EmpathyIndividualStore *store)
{
  EmpathyIndividualStoreChannel *self = EMPATHY_INDIVIDUAL_STORE_CHANNEL (
      store);
  GPtrArray *members;
  GList *list, *l;

  /* remove all. The list returned by g_hash_table_get_keys() is valid until
   * the hash table is modified so we can't remove the contact directly in the
   * iteration. */
  members = g_ptr_array_new_with_free_func (g_object_unref);

  list = g_hash_table_get_keys (self->priv->individuals);
  for (l = list; l != NULL; l = g_list_next (l))
    {
      g_ptr_array_add (members, g_object_ref (l->data));
    }

  remove_members (self, members);

  g_list_free (list);
  g_ptr_array_unref (members);

  /* re-add members */
  members = tp_channel_group_dup_members_contacts (self->priv->channel);
  if (members == NULL)
    return;

  add_members (self, members);
  g_ptr_array_unref (members);
}

static void
empathy_individual_store_channel_class_init (
    EmpathyIndividualStoreChannelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  EmpathyIndividualStoreClass *store_class = EMPATHY_INDIVIDUAL_STORE_CLASS (
      klass);

  object_class->dispose = individual_store_channel_dispose;
  object_class->get_property = individual_store_channel_get_property;
  object_class->set_property = individual_store_channel_set_property;

  store_class->reload_individuals = individual_store_channel_reload_individuals;

  g_object_class_install_property (object_class,
      PROP_CHANNEL,
      g_param_spec_object ("individual-channel",
          "Individual channel",
          "Individual channel",
          TP_TYPE_CHANNEL,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_type_class_add_private (object_class,
      sizeof (EmpathyIndividualStoreChannelPriv));
}

static void
empathy_individual_store_channel_init (EmpathyIndividualStoreChannel *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_INDIVIDUAL_STORE_CHANNEL, EmpathyIndividualStoreChannelPriv);

  self->priv->individuals = g_hash_table_new_full (NULL, NULL, g_object_unref,
      g_object_unref);
}

EmpathyIndividualStoreChannel *
empathy_individual_store_channel_new (TpChannel *channel)
{
  g_return_val_if_fail (TP_IS_CHANNEL (channel), NULL);

  return g_object_new (EMPATHY_TYPE_INDIVIDUAL_STORE_CHANNEL,
      "individual-channel", channel, NULL);
}

TpChannel *
empathy_individual_store_channel_get_channel (
    EmpathyIndividualStoreChannel *self)
{
  g_return_val_if_fail (EMPATHY_IS_INDIVIDUAL_STORE_CHANNEL (self), FALSE);

  return self->priv->channel;
}
