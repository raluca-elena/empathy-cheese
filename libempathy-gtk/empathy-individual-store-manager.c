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
#include <libempathy/empathy-individual-manager.h>

#include "empathy-individual-store-manager.h"

#include "empathy-ui-utils.h"
#include "empathy-gtk-enum-types.h"

#define DEBUG_FLAG EMPATHY_DEBUG_CONTACT
#include <libempathy/empathy-debug.h>

struct _EmpathyIndividualStoreManagerPriv
{
  EmpathyIndividualManager *manager;
  gboolean setup_idle_id;
};

enum
{
  PROP_0,
  PROP_INDIVIDUAL_MANAGER,
};


G_DEFINE_TYPE (EmpathyIndividualStoreManager, empathy_individual_store_manager,
    EMPATHY_TYPE_INDIVIDUAL_STORE);

static void
individual_store_manager_members_changed_cb (EmpathyIndividualManager *manager,
    const gchar *message,
    GList *added,
    GList *removed,
    guint reason,
    EmpathyIndividualStoreManager *self)
{
  GList *l;
  EmpathyIndividualStore *store = EMPATHY_INDIVIDUAL_STORE (self);

  for (l = removed; l; l = l->next)
    {
      DEBUG ("Individual %s %s",
          folks_individual_get_id (l->data), "removed");

      individual_store_remove_individual_and_disconnect (store, l->data);
    }

  for (l = added; l; l = l->next)
    {
      DEBUG ("Individual %s %s", folks_individual_get_id (l->data), "added");

      individual_store_add_individual_and_connect (store, l->data);
    }
}

static void
individual_store_manager_groups_changed_cb (EmpathyIndividualManager *manager,
    FolksIndividual *individual,
    gchar *group,
    gboolean is_member,
    EmpathyIndividualStoreManager *self)
{
  EmpathyIndividualStore *store = EMPATHY_INDIVIDUAL_STORE (self);

  DEBUG ("Updating groups for individual %s",
      folks_individual_get_id (individual));

  /* We do this to make sure the groups are correct, if not, we
   * would have to check the groups already set up for each
   * contact and then see what has been updated.
   */
  empathy_individual_store_refresh_individual (store, individual);
}

static gboolean
individual_store_manager_manager_setup (gpointer user_data)
{
  EmpathyIndividualStoreManager *self = user_data;
  EmpathyIndividualStore *store = user_data;
  GList *individuals;

  /* Signal connection. */

  DEBUG ("handling individual renames unimplemented");

  g_signal_connect (self->priv->manager,
      "members-changed",
      G_CALLBACK (individual_store_manager_members_changed_cb), self);

  g_signal_connect (self->priv->manager,
      "groups-changed",
      G_CALLBACK (individual_store_manager_groups_changed_cb), self);

  /* Add contacts already created. */
  individuals = empathy_individual_manager_get_members (self->priv->manager);
  if (individuals != NULL)
    {
      individual_store_manager_members_changed_cb (self->priv->manager, "initial add",
          individuals, NULL, 0, self);
      g_list_free (individuals);
    }

  self->priv->setup_idle_id = 0;
  return FALSE;
}

static void
individual_store_manager_set_individual_manager (
    EmpathyIndividualStoreManager *self,
    EmpathyIndividualManager *manager)
{
  EmpathyIndividualStore *store = EMPATHY_INDIVIDUAL_STORE (self);

  g_assert (self->priv->manager == NULL); /* construct only */
  self->priv->manager = g_object_ref (manager);

  /* Let a chance to have all properties set before populating */
  self->priv->setup_idle_id = g_idle_add (
      individual_store_manager_manager_setup, self);
}

static void
individual_store_manager_member_renamed_cb (EmpathyIndividualManager *manager,
    FolksIndividual *old_individual,
    FolksIndividual *new_individual,
    guint reason,
    const gchar *message,
    EmpathyIndividualStoreManager *self)
{
  EmpathyIndividualStore *store = EMPATHY_INDIVIDUAL_STORE (self);

  DEBUG ("Individual %s renamed to %s",
      folks_individual_get_id (old_individual),
      folks_individual_get_id (new_individual));

  /* remove old contact */
  individual_store_remove_individual_and_disconnect (store, old_individual);

  /* add the new contact */
  individual_store_add_individual_and_connect (store, new_individual);
}

static void
individual_store_manager_dispose (GObject *object)
{
  EmpathyIndividualStoreManager *self = EMPATHY_INDIVIDUAL_STORE_MANAGER (
      object);
  EmpathyIndividualStore *store = EMPATHY_INDIVIDUAL_STORE (object);
  GList *individuals, *l;

  individuals = empathy_individual_manager_get_members (self->priv->manager);
  for (l = individuals; l; l = l->next)
    {
      empathy_individual_store_disconnect_individual (store,
          FOLKS_INDIVIDUAL (l->data));
    }
  tp_clear_pointer (&individuals, g_list_free);

  if (self->priv->manager != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->priv->manager,
          G_CALLBACK (individual_store_manager_member_renamed_cb), object);
      g_signal_handlers_disconnect_by_func (self->priv->manager,
          G_CALLBACK (individual_store_manager_members_changed_cb), object);
      g_signal_handlers_disconnect_by_func (self->priv->manager,
          G_CALLBACK (individual_store_manager_groups_changed_cb), object);
      g_clear_object (&self->priv->manager);
    }

  if (self->priv->setup_idle_id != 0)
    {
      g_source_remove (self->priv->setup_idle_id);
      self->priv->setup_idle_id = 0;
    }

  G_OBJECT_CLASS (empathy_individual_store_manager_parent_class)->dispose (
      object);
}

static void
individual_store_manager_get_property (GObject *object,
    guint param_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyIndividualStoreManager *self = EMPATHY_INDIVIDUAL_STORE_MANAGER (
      object);

  switch (param_id)
    {
    case PROP_INDIVIDUAL_MANAGER:
      g_value_set_object (value, self->priv->manager);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    };
}

static void
individual_store_manager_set_property (GObject *object,
    guint param_id,
    const GValue *value,
    GParamSpec *pspec)
{
  switch (param_id)
    {
    case PROP_INDIVIDUAL_MANAGER:
      individual_store_manager_set_individual_manager (
          EMPATHY_INDIVIDUAL_STORE_MANAGER (object),
          g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    };
}

static void
individual_store_manager_reload_individuals (EmpathyIndividualStore *store)
{
  EmpathyIndividualStoreManager *self = EMPATHY_INDIVIDUAL_STORE_MANAGER (
      store);
  GList *contacts;

  contacts = empathy_individual_manager_get_members (self->priv->manager);

  individual_store_manager_members_changed_cb (self->priv->manager,
      "re-adding members: toggled group visibility",
      contacts, NULL, 0, self);

  g_list_free (contacts);
}

static gboolean
individual_store_manager_initial_loading (EmpathyIndividualStore *store)
{
  EmpathyIndividualStoreManager *self = EMPATHY_INDIVIDUAL_STORE_MANAGER (
      store);

  return self->priv->setup_idle_id != 0;
}

static void
empathy_individual_store_manager_class_init (
    EmpathyIndividualStoreManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  EmpathyIndividualStoreClass *store_class = EMPATHY_INDIVIDUAL_STORE_CLASS (
      klass);

  object_class->dispose = individual_store_manager_dispose;
  object_class->get_property = individual_store_manager_get_property;
  object_class->set_property = individual_store_manager_set_property;

  store_class->reload_individuals = individual_store_manager_reload_individuals;
  store_class->initial_loading = individual_store_manager_initial_loading;

  g_object_class_install_property (object_class,
      PROP_INDIVIDUAL_MANAGER,
      g_param_spec_object ("individual-manager",
          "Individual manager",
          "Individual manager",
          EMPATHY_TYPE_INDIVIDUAL_MANAGER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_type_class_add_private (object_class,
      sizeof (EmpathyIndividualStoreManagerPriv));
}

static void
empathy_individual_store_manager_init (EmpathyIndividualStoreManager *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_INDIVIDUAL_STORE_MANAGER, EmpathyIndividualStoreManagerPriv);
}

EmpathyIndividualStoreManager *
empathy_individual_store_manager_new (EmpathyIndividualManager *manager)
{
  g_return_val_if_fail (EMPATHY_IS_INDIVIDUAL_MANAGER (manager), NULL);

  return g_object_new (EMPATHY_TYPE_INDIVIDUAL_STORE_MANAGER,
      "individual-manager", manager, NULL);
}

EmpathyIndividualManager *
empathy_individual_store_manager_get_manager (
    EmpathyIndividualStoreManager *self)
{
  g_return_val_if_fail (EMPATHY_IS_INDIVIDUAL_STORE_MANAGER (self), FALSE);

  return self->priv->manager;
}
