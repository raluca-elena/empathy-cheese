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
 */

#ifndef __EMPATHY_MIC_MONITOR_H__
#define __EMPATHY_MIC_MONITOR_H__

#include <glib-object.h>

#include <pulse/pulseaudio.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_MIC_MONITOR         (empathy_mic_monitor_get_type ())
#define EMPATHY_MIC_MONITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_MIC_MONITOR, EmpathyMicMonitor))
#define EMPATHY_MIC_MONITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_MIC_MONITOR, EmpathyMicMonitorClass))
#define EMPATHY_IS_MIC_MONITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_MIC_MONITOR))
#define EMPATHY_IS_MIC_MONITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_MIC_MONITOR))
#define EMPATHY_MIC_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_MIC_MONITOR, EmpathyMicMonitorClass))

typedef struct _EmpathyMicMonitor        EmpathyMicMonitor;
typedef struct _EmpathyMicMonitorPrivate EmpathyMicMonitorPrivate;
typedef struct _EmpathyMicMonitorClass   EmpathyMicMonitorClass;

struct _EmpathyMicMonitor
{
  GObject parent;
  EmpathyMicMonitorPrivate *priv;
};

struct _EmpathyMicMonitorClass
{
  GObjectClass parent_class;
};

GType empathy_mic_monitor_get_type (void) G_GNUC_CONST;

EmpathyMicMonitor * empathy_mic_monitor_new (void);


typedef struct
{
  guint index;
  gchar *name;
  gchar *description;
  gboolean is_monitor;
} EmpathyMicrophone;

void empathy_mic_monitor_list_microphones_async (EmpathyMicMonitor *monitor,
    GAsyncReadyCallback callback, gpointer user_data);
const GList * empathy_mic_monitor_list_microphones_finish (EmpathyMicMonitor *monitor,
    GAsyncResult *result, GError **error);

void empathy_mic_monitor_change_microphone_async (EmpathyMicMonitor *monitor,
    guint source_output_idx, guint source_idx, GAsyncReadyCallback callback, gpointer user_data);
gboolean empathy_mic_monitor_change_microphone_finish (EmpathyMicMonitor *monitor,
    GAsyncResult *result, GError **error);

void empathy_mic_monitor_get_current_mic_async (EmpathyMicMonitor *self,
    guint source_output_idx, GAsyncReadyCallback callback, gpointer user_data);
guint empathy_mic_monitor_get_current_mic_finish (EmpathyMicMonitor *self,
    GAsyncResult *result, GError **error);

void empathy_mic_monitor_get_default_async (EmpathyMicMonitor *self,
    GAsyncReadyCallback callback, gpointer user_data);
const gchar * empathy_mic_monitor_get_default_finish (EmpathyMicMonitor *self,
    GAsyncResult *result, GError **error);

void empathy_mic_monitor_set_default_async (EmpathyMicMonitor *self,
    const gchar *name, GAsyncReadyCallback callback, gpointer user_data);
gboolean empathy_mic_monitor_set_default_finish (EmpathyMicMonitor *self,
    GAsyncResult *result, GError **error);

G_END_DECLS

#endif /* __EMPATHY_MIC_MONITOR_H__ */
