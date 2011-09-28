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
 */

#include <config.h>

#include <gtk/gtk.h>

#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

#include "empathy-mic-monitor.h"


#include <libempathy/empathy-utils.h>

#define DEBUG_FLAG EMPATHY_DEBUG_VOIP
#include <libempathy/empathy-debug.h>

enum
{
  MICROPHONE_ADDED,
  MICROPHONE_REMOVED,
  MICROPHONE_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

struct _EmpathyMicMonitorPrivate
{
  pa_glib_mainloop *loop;
  pa_context *context;
  GQueue *operations;
};

G_DEFINE_TYPE (EmpathyMicMonitor, empathy_mic_monitor, G_TYPE_OBJECT);

typedef void (*OperationFunc) (EmpathyMicMonitor *, GSimpleAsyncResult *);

typedef struct
{
  OperationFunc func;
  GSimpleAsyncResult *result;
} Operation;

static Operation *
operation_new (OperationFunc func,
    GSimpleAsyncResult *result)
{
  Operation *o = g_slice_new0 (Operation);

  o->func = func;
  o->result = result;

  return o;
}

static void
operation_free (Operation *o,
    gboolean cancelled)
{
  if (cancelled)
    {
      g_simple_async_result_set_error (o->result,
          G_IO_ERROR, G_IO_ERROR_CANCELLED,
          "The microphone monitor was disposed");
      g_simple_async_result_complete (o->result);
      g_object_unref (o->result);
    }

  g_slice_free (Operation, o);
}

static void
operations_run (EmpathyMicMonitor *self)
{
  EmpathyMicMonitorPrivate *priv = self->priv;
  pa_context_state_t state = pa_context_get_state (priv->context);
  GList *l;

  if (state != PA_CONTEXT_READY)
    return;

  for (l = priv->operations->head; l != NULL; l = l->next)
    {
      Operation *o = l->data;

      o->func (self, o->result);

      operation_free (o, FALSE);
    }

  g_queue_clear (priv->operations);
}

static void
empathy_mic_monitor_source_output_info_cb (pa_context *context,
    const pa_source_output_info *info,
    int eol,
    void *userdata)
{
  EmpathyMicMonitor *self = userdata;

  if (eol)
    return;

  g_signal_emit (self, signals[MICROPHONE_CHANGED], 0,
      info->index, info->source);
}

static void
empathy_mic_monitor_source_info_cb (pa_context *context,
    const pa_source_info *info,
    int eol,
    void *userdata)
{
  EmpathyMicMonitor *self = userdata;
  gboolean is_monitor;

  if (eol)
    return;

  is_monitor = (info->monitor_of_sink != PA_INVALID_INDEX);

  g_signal_emit (self, signals[MICROPHONE_ADDED], 0,
      info->index, info->name, info->description, is_monitor);
}

static void
empathy_mic_monitor_pa_event_cb (pa_context *context,
    pa_subscription_event_type_t type,
    uint32_t idx,
    void *userdata)
{
  EmpathyMicMonitor *self = userdata;

  if ((type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT
      && (type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_CHANGE)
    {
      /* Microphone in the source output has changed */
      pa_context_get_source_output_info (context, idx,
          empathy_mic_monitor_source_output_info_cb, self);
    }
  else if ((type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SOURCE
      && (type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE)
    {
      /* A mic has been removed */
      g_signal_emit (self, signals[MICROPHONE_REMOVED], 0, idx);
    }
  else if ((type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SOURCE
      && (type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_NEW)
    {
      /* A mic has been plugged in */
      pa_context_get_source_info_by_index (context, idx,
          empathy_mic_monitor_source_info_cb, self);
    }
}

static void
empathy_mic_monitor_pa_subscribe_cb (pa_context *context,
    int success,
    void *userdata)
{
  if (!success)
    DEBUG ("Failed to subscribe to PulseAudio events");
}

static void
empathy_mic_monitor_pa_state_change_cb (pa_context *context,
    void *userdata)
{
  EmpathyMicMonitor *self = userdata;
  EmpathyMicMonitorPrivate *priv = self->priv;
  pa_context_state_t state = pa_context_get_state (priv->context);

  if (state == PA_CONTEXT_READY)
    {
      /* Listen to pulseaudio events so we know when sources are
       * added and when the microphone is changed. */
      pa_context_set_subscribe_callback (priv->context,
          empathy_mic_monitor_pa_event_cb, self);
      pa_context_subscribe (priv->context,
          PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT,
          empathy_mic_monitor_pa_subscribe_cb, NULL);

      operations_run (self);
    }
}

static void
empathy_mic_monitor_init (EmpathyMicMonitor *self)
{
  EmpathyMicMonitorPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
    EMPATHY_TYPE_MIC_MONITOR, EmpathyMicMonitorPrivate);

  self->priv = priv;
}

static void
empathy_mic_monitor_constructed (GObject *obj)
{
  EmpathyMicMonitor *self = EMPATHY_MIC_MONITOR (obj);
  EmpathyMicMonitorPrivate *priv = self->priv;

  /* PulseAudio stuff: We need to create a dummy pa_glib_mainloop* so
   * Pulse can use the mainloop that GTK has created for us. */
  priv->loop = pa_glib_mainloop_new (NULL);
  priv->context = pa_context_new (pa_glib_mainloop_get_api (priv->loop),
      "EmpathyMicMonitor");

  /* Finally listen for state changes so we know when we've
   * connected. */
  pa_context_set_state_callback (priv->context,
      empathy_mic_monitor_pa_state_change_cb, obj);
  pa_context_connect (priv->context, NULL, 0, NULL);

  priv->operations = g_queue_new ();
}

static void
empathy_mic_monitor_dispose (GObject *obj)
{
  EmpathyMicMonitor *self = EMPATHY_MIC_MONITOR (obj);
  EmpathyMicMonitorPrivate *priv = self->priv;

  g_queue_foreach (priv->operations, (GFunc) operation_free,
      GUINT_TO_POINTER (TRUE));
  g_queue_free (priv->operations);

  if (priv->context != NULL)
    pa_context_unref (priv->context);
  priv->context = NULL;

  if (priv->loop != NULL)
    pa_glib_mainloop_free (priv->loop);
  priv->loop = NULL;

  G_OBJECT_CLASS (empathy_mic_monitor_parent_class)->dispose (obj);
}

static void
empathy_mic_monitor_class_init (EmpathyMicMonitorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = empathy_mic_monitor_constructed;
  object_class->dispose = empathy_mic_monitor_dispose;

  signals[MICROPHONE_ADDED] = g_signal_new ("microphone-added",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_generic,
    G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);

  signals[MICROPHONE_REMOVED] = g_signal_new ("microphone-removed",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_generic,
    G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[MICROPHONE_CHANGED] = g_signal_new ("microphone-changed",
    G_TYPE_FROM_CLASS (klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    g_cclosure_marshal_generic,
    G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  g_type_class_add_private (object_class, sizeof (EmpathyMicMonitorPrivate));
}

EmpathyMicMonitor *
empathy_mic_monitor_new (void)
{
  return g_object_new (EMPATHY_TYPE_MIC_MONITOR,
      NULL);
}

/* operation: list microphones */
static void
operation_list_microphones_free (gpointer data)
{
  GQueue *queue = data;
  GList *l;

  for (l = queue->head; l != NULL; l = l->next)
    {
      EmpathyMicrophone *mic = l->data;

      g_free (mic->name);
      g_free (mic->description);
      g_slice_free (EmpathyMicrophone, mic);
    }

  g_queue_free (queue);
}

static void
operation_list_microphones_cb (pa_context *context,
    const pa_source_info *info,
    int eol,
    void *userdata)
{
  GSimpleAsyncResult *result = userdata;
  EmpathyMicrophone *mic;
  GQueue *queue;

  if (eol)
    {
      g_simple_async_result_complete (result);
      g_object_unref (result);
      return;
    }

  mic = g_slice_new0 (EmpathyMicrophone);
  mic->index = info->index;
  mic->name = g_strdup (info->name);
  mic->description = g_strdup (info->description);
  mic->is_monitor = (info->monitor_of_sink != PA_INVALID_INDEX);

  /* add it to the queue */
  queue = g_simple_async_result_get_op_res_gpointer (result);
  g_queue_push_tail (queue, mic);
}

static void
operation_list_microphones (EmpathyMicMonitor *self,
    GSimpleAsyncResult *result)
{
  EmpathyMicMonitorPrivate *priv = self->priv;

  g_assert_cmpuint (pa_context_get_state (priv->context), ==, PA_CONTEXT_READY);

  g_simple_async_result_set_op_res_gpointer (result, g_queue_new (),
      operation_list_microphones_free);

  pa_context_get_source_info_list (priv->context,
      operation_list_microphones_cb, result);
}

void
empathy_mic_monitor_list_microphones_async (EmpathyMicMonitor *self,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
EmpathyMicMonitorPrivate *priv = self->priv;
  Operation *operation;
  GSimpleAsyncResult *simple;

  simple = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
      empathy_mic_monitor_list_microphones_async);

  operation = operation_new (operation_list_microphones, simple);
  g_queue_push_tail (priv->operations, operation);

  /* gogogogo */
  operations_run (self);
}

const GList *
empathy_mic_monitor_list_microphones_finish (EmpathyMicMonitor *src,
    GAsyncResult *result,
    GError **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);
  GQueue *queue;

  if (g_simple_async_result_propagate_error (simple, error))
      return NULL;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
          G_OBJECT (src), empathy_mic_monitor_list_microphones_async),
      NULL);

  queue = g_simple_async_result_get_op_res_gpointer (simple);
  return queue->head;
}

/* operation: change microphone */
typedef struct
{
  guint source_output_idx;
  guint source_idx;
} ChangeMicrophoneData;

static void
operation_change_microphone_cb (pa_context *context,
    int success,
    void *userdata)
{
  GSimpleAsyncResult *result = userdata;

  if (!success)
    {
      g_simple_async_result_set_error (result, G_IO_ERROR, G_IO_ERROR_FAILED,
          "Failed to change microphone. Reason unknown.");
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

static void
operation_change_microphone (EmpathyMicMonitor *self,
    GSimpleAsyncResult *result)
{
  EmpathyMicMonitorPrivate *priv = self->priv;
  ChangeMicrophoneData *data;

  g_assert_cmpuint (pa_context_get_state (priv->context), ==, PA_CONTEXT_READY);

  data = g_simple_async_result_get_op_res_gpointer (result);

  pa_context_move_source_output_by_index (priv->context,
      data->source_output_idx, data->source_idx,
      operation_change_microphone_cb, result);

  g_simple_async_result_set_op_res_gpointer (result, NULL, NULL);
  g_slice_free (ChangeMicrophoneData, data);
}

void
empathy_mic_monitor_change_microphone_async (EmpathyMicMonitor *self,
    guint source_output_idx,
    guint source_idx,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  EmpathyMicMonitorPrivate *priv = self->priv;
  GSimpleAsyncResult *simple;
  Operation *operation;
  ChangeMicrophoneData *data;

  simple = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
      empathy_mic_monitor_change_microphone_async);

  if (source_output_idx == PA_INVALID_INDEX)
    {
      g_simple_async_result_set_error (simple, G_IO_ERROR, G_IO_ERROR_FAILED,
          "Invalid source output index");
      g_simple_async_result_complete_in_idle (simple);
      g_object_unref (simple);
      return;
    }

  data = g_slice_new0 (ChangeMicrophoneData);
  data->source_idx = source_idx;
  data->source_output_idx = source_output_idx;
  g_simple_async_result_set_op_res_gpointer (simple, data, NULL);

  operation = operation_new (operation_change_microphone, simple);
  g_queue_push_tail (priv->operations, operation);

  /* gogogogo */
  operations_run (self);
}

gboolean
empathy_mic_monitor_change_microphone_finish (EmpathyMicMonitor *self,
    GAsyncResult *result,
    GError **error)
{
  empathy_implement_finish_void (self,
      empathy_mic_monitor_change_microphone_async);
}

/* operation: get current mic */
static void
empathy_mic_monitor_get_current_mic_cb (pa_context *context,
    const pa_source_output_info *info,
    int eol,
    void *userdata)
{
  GSimpleAsyncResult *result = userdata;

  if (eol)
    return;

  if (g_simple_async_result_get_op_res_gpointer (result) != NULL)
    return;

  g_simple_async_result_set_op_res_gpointer (result,
      GUINT_TO_POINTER (info->source), NULL);
  g_simple_async_result_complete (result);
  g_object_unref (result);
}

static void
operation_get_current_mic (EmpathyMicMonitor *self,
    GSimpleAsyncResult *result)
{
  EmpathyMicMonitorPrivate *priv = self->priv;
  guint source_output_idx;

  g_assert_cmpuint (pa_context_get_state (priv->context), ==, PA_CONTEXT_READY);

  source_output_idx = GPOINTER_TO_UINT (
      g_simple_async_result_get_op_res_gpointer (result));

  /* unset this so we can use it in the cb */
  g_simple_async_result_set_op_res_gpointer (result, NULL, NULL);

  pa_context_get_source_output_info (priv->context, source_output_idx,
      empathy_mic_monitor_get_current_mic_cb, result);
}

void
empathy_mic_monitor_get_current_mic_async (EmpathyMicMonitor *self,
    guint source_output_idx,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  EmpathyMicMonitorPrivate *priv = self->priv;
  Operation *operation;
  GSimpleAsyncResult *simple;

  simple = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
      empathy_mic_monitor_get_current_mic_async);

  g_simple_async_result_set_op_res_gpointer (simple,
      GUINT_TO_POINTER (source_output_idx), NULL);

  operation = operation_new (operation_get_current_mic, simple);
  g_queue_push_tail (priv->operations, operation);

  operations_run (self);
}

guint
empathy_mic_monitor_get_current_mic_finish (EmpathyMicMonitor *self,
    GAsyncResult *result,
    GError **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return PA_INVALID_INDEX;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
          G_OBJECT (self), empathy_mic_monitor_get_current_mic_async),
      PA_INVALID_INDEX);

  return GPOINTER_TO_UINT (
      g_simple_async_result_get_op_res_gpointer (simple));
}

/* operation: get default */
static void
empathy_mic_monitor_get_default_cb (pa_context *context,
    const pa_server_info *info,
    void *userdata)
{
  GSimpleAsyncResult *result = userdata;

  /* TODO: it would be nice in future, for consistency, if this gave
   * the source idx instead of the name. */
  g_simple_async_result_set_op_res_gpointer (result,
      g_strdup (info->default_source_name), g_free);
  g_simple_async_result_complete (result);
  g_object_unref (result);
}

static void
operation_get_default (EmpathyMicMonitor *self,
    GSimpleAsyncResult *result)
{
  EmpathyMicMonitorPrivate *priv = self->priv;

  g_assert_cmpuint (pa_context_get_state (priv->context), ==, PA_CONTEXT_READY);

  /* unset this so we can use it in the cb */
  g_simple_async_result_set_op_res_gpointer (result, NULL, NULL);

  pa_context_get_server_info (priv->context, empathy_mic_monitor_get_default_cb,
      result);
}

void
empathy_mic_monitor_get_default_async (EmpathyMicMonitor *self,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  EmpathyMicMonitorPrivate *priv = self->priv;
  Operation *operation;
  GSimpleAsyncResult *simple;

  simple = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
      empathy_mic_monitor_get_default_async);

  operation = operation_new (operation_get_default, simple);
  g_queue_push_tail (priv->operations, operation);

  operations_run (self);
}

const gchar *
empathy_mic_monitor_get_default_finish (EmpathyMicMonitor *self,
    GAsyncResult *result,
    GError **error)
{
  empathy_implement_finish_return_pointer (self,
      empathy_mic_monitor_get_default_async);
}

/* operation: set default */
static void
empathy_mic_monitor_set_default_cb (pa_context *c,
    int success,
    void *userdata)
{
  GSimpleAsyncResult *result = userdata;

  if (!success)
    {
      g_simple_async_result_set_error (result,
          G_IO_ERROR, G_IO_ERROR_FAILED,
          "The operation failed for an unknown reason");
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

static void
operation_set_default (EmpathyMicMonitor *self,
    GSimpleAsyncResult *result)
{
  EmpathyMicMonitorPrivate *priv = self->priv;
  gchar *name;

  g_assert_cmpuint (pa_context_get_state (priv->context), ==, PA_CONTEXT_READY);

  name = g_simple_async_result_get_op_res_gpointer (result);

  pa_context_set_default_source (priv->context, name,
      empathy_mic_monitor_set_default_cb, result);
}

void
empathy_mic_monitor_set_default_async (EmpathyMicMonitor *self,
    const gchar *name,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  EmpathyMicMonitorPrivate *priv = self->priv;
  Operation *operation;
  GSimpleAsyncResult *simple;

  simple = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
      empathy_mic_monitor_set_default_async);

  g_simple_async_result_set_op_res_gpointer (simple, g_strdup (name), g_free);

  operation = operation_new (operation_set_default, simple);
  g_queue_push_tail (priv->operations, operation);

  operations_run (self);
}

gboolean
empathy_mic_monitor_set_default_finish (EmpathyMicMonitor *self,
    GAsyncResult *result,
    GError **error)
{
  empathy_implement_finish_void (self,
      empathy_mic_monitor_set_default_async);
}
