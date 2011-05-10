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

#ifndef __EMPATHY_CAMERA_MONITOR_H__
#define __EMPATHY_CAMERA_MONITOR_H__

#include <glib-object.h>

G_BEGIN_DECLS
#define EMPATHY_TYPE_CAMERA_MONITOR         (empathy_camera_monitor_get_type ())
#define EMPATHY_CAMERA_MONITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_CAMERA_MONITOR, EmpathyCameraMonitor))
#define EMPATHY_CAMERA_MONITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_CAMERA_MONITOR, EmpathyCameraMonitorClass))
#define EMPATHY_IS_CAMERA_MONITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_CAMERA_MONITOR))
#define EMPATHY_IS_CAMERA_MONITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_CAMERA_MONITOR))
#define EMPATHY_CAMERA_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_CAMERA_MONITOR, EmpathyCameraMonitorClass))

typedef struct _EmpathyCameraMonitor EmpathyCameraMonitor;
typedef struct _EmpathyCameraMonitorClass EmpathyCameraMonitorClass;
typedef struct _EmpathyCameraMonitorPrivate EmpathyCameraMonitorPrivate;

struct _EmpathyCameraMonitor
{
  GObject parent;
  EmpathyCameraMonitorPrivate *priv;
};

struct _EmpathyCameraMonitorClass
{
  GObjectClass parent_class;
};

GType empathy_camera_monitor_get_type (void) G_GNUC_CONST;

EmpathyCameraMonitor *empathy_camera_monitor_dup_singleton (void);

gboolean empathy_camera_monitor_get_available (EmpathyCameraMonitor *self);

G_END_DECLS
#endif /* __EMPATHY_CAMERA_MONITOR_H__ */
