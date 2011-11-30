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
  GQueue *cameras;
  gint num_cameras;
};

enum
{
  PROP_0,
  PROP_AVAILABLE,
};

enum
{
  CAMERA_ADDED,
  CAMERA_REMOVED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (EmpathyCameraMonitor, empathy_camera_monitor, G_TYPE_OBJECT);

static EmpathyCameraMonitor *manager_singleton = NULL;

static EmpathyCamera *
empathy_camera_new (const gchar *id,
    const gchar *device,
    const gchar *name)
{
  EmpathyCamera *camera = g_slice_new (EmpathyCamera);

  camera->id = g_strdup (id);
  camera->device = g_strdup (device);
  camera->name = g_strdup (name);

  return camera;
}

static EmpathyCamera *
empathy_camera_copy (EmpathyCamera *camera)
{
  return empathy_camera_new (camera->id, camera->device, camera->name);
}

static void
empathy_camera_free (EmpathyCamera *camera)
{
  g_free (camera->id);
  g_free (camera->device);
  g_free (camera->name);

  g_slice_free (EmpathyCamera, camera);
}

G_DEFINE_BOXED_TYPE (EmpathyCamera, empathy_camera,
    empathy_camera_copy, empathy_camera_free)

static gint
empathy_camera_find (gconstpointer a,
    gconstpointer b)
{
  const EmpathyCamera *camera = a;
  const gchar *id = b;

  return g_strcmp0 (camera->id, id);
}

static void
empathy_camera_monitor_free_camera_foreach (gpointer data,
    gpointer user_data)
{
  empathy_camera_free (data);
}

static void
on_camera_added (CheeseCameraDeviceMonitor *device,
    gchar *id,
    gchar *filename,
    gchar *product_name,
    gint api_version,
    EmpathyCameraMonitor *self)
{
  EmpathyCamera *camera;

  if (self->priv->cameras == NULL)
    return;

  camera = empathy_camera_new (id, filename, product_name);

  g_queue_push_tail (self->priv->cameras, camera);

  self->priv->num_cameras++;

  if (self->priv->num_cameras == 1)
    g_object_notify (G_OBJECT (self), "available");

  g_signal_emit (self, signals[CAMERA_ADDED], 0, camera);
}

static void
on_camera_removed (CheeseCameraDeviceMonitor *device,
    gchar *id,
    EmpathyCameraMonitor *self)
{
  EmpathyCamera *camera;
  GList *l;

  if (self->priv->cameras == NULL)
    return;

  l = g_queue_find_custom (self->priv->cameras, id, empathy_camera_find);

  g_return_if_fail (l != NULL);

  camera = l->data;

  g_queue_delete_link (self->priv->cameras, l);

  self->priv->num_cameras--;

  if (self->priv->num_cameras == 0)
    g_object_notify (G_OBJECT (self), "available");

  g_signal_emit (self, signals[CAMERA_REMOVED], 0, camera);

  empathy_camera_free (camera);
}

const GList *
empathy_camera_monitor_get_cameras (EmpathyCameraMonitor *self)
{
  if (self->priv->cameras != NULL)
    return self->priv->cameras->head;
  else
    return NULL;
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

  g_queue_foreach (self->priv->cameras,
      empathy_camera_monitor_free_camera_foreach, NULL);
  tp_clear_pointer (&self->priv->cameras, g_queue_free);

  G_OBJECT_CLASS (empathy_camera_monitor_parent_class)->dispose (object);
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
  object_class->constructed = empathy_camera_monitor_constructed;
  object_class->get_property = empathy_camera_monitor_get_property;

  g_object_class_install_property (object_class, PROP_AVAILABLE,
      g_param_spec_boolean ("available", "Available",
      "Camera available", TRUE, G_PARAM_READABLE));

  signals[CAMERA_ADDED] =
      g_signal_new ("added", G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
          0, NULL, NULL,
          g_cclosure_marshal_generic,
          G_TYPE_NONE, 1, EMPATHY_TYPE_CAMERA);

  signals[CAMERA_REMOVED] =
      g_signal_new ("removed", G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
          0, NULL, NULL,
          g_cclosure_marshal_generic,
          G_TYPE_NONE, 1, EMPATHY_TYPE_CAMERA);

  g_type_class_add_private (object_class,
      sizeof (EmpathyCameraMonitorPrivate));
}

static void
empathy_camera_monitor_init (EmpathyCameraMonitor *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_CAMERA_MONITOR, EmpathyCameraMonitorPrivate);

  self->priv->cameras = g_queue_new ();

  self->priv->cheese_monitor = cheese_camera_device_monitor_new ();

  g_signal_connect (self->priv->cheese_monitor, "added",
      G_CALLBACK (on_camera_added), self);
  g_signal_connect (self->priv->cheese_monitor, "removed",
      G_CALLBACK (on_camera_removed), self);

#ifndef HAVE_UDEV
  /* No udev, assume there are cameras present */
  self->priv->num_cameras = 1;
#endif
}

EmpathyCameraMonitor *
empathy_camera_monitor_dup_singleton (void)
{
  GObject *retval;

  if (manager_singleton)
    {
      retval = g_object_ref (manager_singleton);
    }
  else
    {
      retval = g_object_new (EMPATHY_TYPE_CAMERA_MONITOR, NULL);

      manager_singleton = EMPATHY_CAMERA_MONITOR (retval);
      g_object_add_weak_pointer (retval, (gpointer) &manager_singleton);
    }

  return EMPATHY_CAMERA_MONITOR (retval);
}

EmpathyCameraMonitor *
empathy_camera_monitor_new (void)
{
  return EMPATHY_CAMERA_MONITOR (
      g_object_new (EMPATHY_TYPE_CAMERA_MONITOR, NULL));
}

gboolean empathy_camera_monitor_get_available (EmpathyCameraMonitor *self)
{
  g_return_val_if_fail (EMPATHY_IS_CAMERA_MONITOR (self), FALSE);

  return self->priv->num_cameras > 0;
}
