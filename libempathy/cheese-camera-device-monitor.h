/* This file is a copy of cheese-camera-device-monitor.h from Empathy. We
 * just renamespaced it to avoid conflicts when linking on libcheese. */
/*
 * Copyright © 2007,2008 Jaap Haitsma <jaap@haitsma.org>
 * Copyright © 2007-2009 daniel g. siegel <dgsiegel@gnome.org>
 * Copyright © 2008 Ryan zeigler <zeiglerr@gmail.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef __EMPATHY_CAMERA_DEVICE_MONITOR_H__
#define __EMPATHY_CAMERA_DEVICE_MONITOR_H__

#include <glib-object.h>
#include <gst/interfaces/xoverlay.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_CAMERA_DEVICE_MONITOR (empathy_camera_device_monitor_get_type ())
#define EMPATHY_CAMERA_DEVICE_MONITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_CAMERA_DEVICE_MONITOR, \
                                                                               EmpathyCameraDeviceMonitor))
#define EMPATHY_CAMERA_DEVICE_MONITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), EMPATHY_TYPE_CAMERA_DEVICE_MONITOR, \
                                                                            EmpathyCameraDeviceMonitorClass))
#define EMPATHY_IS_CAMERA_DEVICE_MONITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_CAMERA_DEVICE_MONITOR))
#define EMPATHY_IS_CAMERA_DEVICE_MONITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_CAMERA_DEVICE_MONITOR))
#define EMPATHY_CAMERA_DEVICE_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_CAMERA_DEVICE_MONITOR, \
                                                                              EmpathyCameraDeviceMonitorClass))

typedef struct _EmpathyCameraDeviceMonitorClass EmpathyCameraDeviceMonitorClass;
typedef struct _EmpathyCameraDeviceMonitor EmpathyCameraDeviceMonitor;

struct _EmpathyCameraDeviceMonitor
{
  GObject parent;
};

struct _EmpathyCameraDeviceMonitorClass
{
  GObjectClass parent_class;

  void (*added)(EmpathyCameraDeviceMonitor *camera,
                const char                *id,
                const char                *device_file,
                const char                *product_name,
                int                        api_version);
  void (*removed)(EmpathyCameraDeviceMonitor *camera, const char *id);
};

GType                      empathy_camera_device_monitor_get_type (void) G_GNUC_CONST;
EmpathyCameraDeviceMonitor *empathy_camera_device_monitor_new (void);
void                       empathy_camera_device_monitor_coldplug (EmpathyCameraDeviceMonitor *monitor);

G_END_DECLS

#endif /* __EMPATHY_CAMERA_DEVICE_MONITOR_H__ */
