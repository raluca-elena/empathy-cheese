/*
 * Copyright (C) 2011 Collabora Ltd.
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
 * Authors: Emilio Pozuelo Monfort <emilio.pozuelo@collabora.co.uk>
 */

#include <config.h>

#include <string.h>

#include <telepathy-glib/util.h>

#include "empathy-camera-monitor.h"
#include "cheese-camera-device-monitor.h"
#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include "empathy-debug.h"

struct _EmpathyCameraMonitorPrivate
{
  CheeseCameraDeviceMonitor *cheese_monitor;
  gint num_cameras;
};

enum
{
  PROP_0,
  PROP_AVAILABLE,
};

G_DEFINE_TYPE (EmpathyCameraMonitor, empathy_camera_monitor, G_TYPE_OBJECT);

static EmpathyCameraMonitor *manager_singleton = NULL;

static void
on_camera_added (CheeseCameraDeviceMonitor *device,
    gchar *id,
    gchar *filename,
    gchar *product_name,
    gint api_version,
    EmpathyCameraMonitor *self)
{
  self->priv->num_cameras++;

  if (self->priv->num_cameras == 1)
    g_object_notify (G_OBJECT (self), "available");
}

static void
on_camera_removed (CheeseCameraDeviceMonitor *device,
    gchar *id,
    EmpathyCameraMonitor *self)
{
  self->priv->num_cameras--;

  if (self->priv->num_cameras == 0)
    g_object_notify (G_OBJECT (self), "available");
}

static void
empathy_camera_monitor_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyCameraMonitor *self = (EmpathyCameraMonitor *) object;

  switch (prop_id)
    {
    case PROP_AVAILABLE:
      g_value_set_boolean (value, self->priv->num_cameras > 0);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
empathy_camera_monitor_dispose (GObject *object)
{
  EmpathyCameraMonitor *self = EMPATHY_CAMERA_MONITOR (object);

  tp_clear_object (&self->priv->cheese_monitor);

  G_OBJECT_CLASS (empathy_camera_monitor_parent_class)->dispose (object);
}

static GObject *
empathy_camera_monitor_constructor (GType type,
    guint n_props,
    GObjectConstructParam *props)
{
  GObject *retval;

  if (manager_singleton)
    {
      retval = g_object_ref (manager_singleton);
    }
  else
    {
      retval =
          G_OBJECT_CLASS (empathy_camera_monitor_parent_class)->
          constructor (type, n_props, props);

      manager_singleton = EMPATHY_CAMERA_MONITOR (retval);
      g_object_add_weak_pointer (retval, (gpointer) & manager_singleton);
    }

  return retval;
}

static void
empathy_camera_monitor_constructed (GObject *object)
{
  EmpathyCameraMonitor *self = (EmpathyCameraMonitor *) object;

  G_OBJECT_CLASS (empathy_camera_monitor_parent_class)->constructed (object);

  cheese_camera_device_monitor_coldplug (self->priv->cheese_monitor);
}

static void
empathy_camera_monitor_class_init (EmpathyCameraMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = empathy_camera_monitor_dispose;
  object_class->constructor = empathy_camera_monitor_constructor;
  object_class->constructed = empathy_camera_monitor_constructed;
  object_class->get_property = empathy_camera_monitor_get_property;

  g_object_class_install_property (object_class, PROP_AVAILABLE,
      g_param_spec_boolean ("available", "Available",
      "Camera available", TRUE, G_PARAM_READABLE));

  g_type_class_add_private (object_class,
      sizeof (EmpathyCameraMonitorPrivate));
}

static void
empathy_camera_monitor_init (EmpathyCameraMonitor *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_CAMERA_MONITOR, EmpathyCameraMonitorPrivate);

  self->priv->cheese_monitor = cheese_camera_device_monitor_new ();

  g_signal_connect (self->priv->cheese_monitor, "added",
      G_CALLBACK (on_camera_added), self);
  g_signal_connect (self->priv->cheese_monitor, "removed",
      G_CALLBACK (on_camera_removed), self);
}

EmpathyCameraMonitor *
empathy_camera_monitor_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_CAMERA_MONITOR, NULL);
}
