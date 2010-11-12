/*
 * Copyright (C) 2010 Collabora Ltd.
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
 * Authors: Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
 */

#include "empathy-channel-factory.h"

#include <telepathy-glib/telepathy-glib.h>

static void factory_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (EmpathyChannelFactory, empathy_channel_factory,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CLIENT_CHANNEL_FACTORY, factory_iface_init))

struct _EmpathyChannelFactoryPrivate
{
  TpClientChannelFactory *automatic_factory;
};

static void
empathy_channel_factory_dispose (GObject *object)
{
  EmpathyChannelFactory *self = EMPATHY_CHANNEL_FACTORY (object);
  void (*dispose) (GObject *) =
    G_OBJECT_CLASS (empathy_channel_factory_parent_class)->dispose;

  tp_clear_object (&self->priv->automatic_factory);

  if (dispose != NULL)
    dispose (object);
}

static void
empathy_channel_factory_class_init (EmpathyChannelFactoryClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  g_type_class_add_private (cls, sizeof (EmpathyChannelFactoryPrivate));

  object_class->dispose = empathy_channel_factory_dispose;
}

static void
empathy_channel_factory_init (EmpathyChannelFactory *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_CHANNEL_FACTORY, EmpathyChannelFactoryPrivate);

  self->priv->automatic_factory = TP_CLIENT_CHANNEL_FACTORY (
      tp_automatic_proxy_factory_dup ());
}

EmpathyChannelFactory *
empathy_channel_factory_new (void)
{
    return g_object_new (EMPATHY_TYPE_CHANNEL_FACTORY,
        NULL);
}

EmpathyChannelFactory *
empathy_channel_factory_dup (void)
{
  static EmpathyChannelFactory *singleton = NULL;

  if (singleton != NULL)
    return g_object_ref (singleton);

  singleton = empathy_channel_factory_new ();

  g_object_add_weak_pointer (G_OBJECT (singleton), (gpointer) &singleton);

  return singleton;
}

static TpChannel *
empathy_channel_factory_create_channel (
    TpClientChannelFactory *factory,
    TpConnection *conn,
    const gchar *path,
    GHashTable *properties,
    GError **error)
{
  EmpathyChannelFactory *self = (EmpathyChannelFactory *) factory;

  return tp_client_channel_factory_create_channel (
      self->priv->automatic_factory, conn, path, properties, error);
}

static GArray *
empathy_channel_factory_dup_channel_features (
    TpClientChannelFactory *factory,
    TpChannel *channel)
{
  EmpathyChannelFactory *self = (EmpathyChannelFactory *) factory;

  return tp_client_channel_factory_dup_channel_features (
      self->priv->automatic_factory, channel);
}

static void
factory_iface_init (gpointer g_iface,
    gpointer unused G_GNUC_UNUSED)
{
  TpClientChannelFactoryInterface *iface = g_iface;

  iface->obj_create_channel = empathy_channel_factory_create_channel;
  iface->obj_dup_channel_features =
    empathy_channel_factory_dup_channel_features;
}
