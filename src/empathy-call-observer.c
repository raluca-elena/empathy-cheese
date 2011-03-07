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

#include <telepathy-glib/telepathy-glib.h>

#include <telepathy-yell/telepathy-yell.h>

#include <libempathy/empathy-channel-factory.h>
#include <libempathy/empathy-utils.h>

#include <extensions/extensions.h>

#include "empathy-call-observer.h"

#define DEBUG_FLAG EMPATHY_DEBUG_DISPATCHER
#include <libempathy/empathy-debug.h>

struct _EmpathyCallObserverPriv {
  TpBaseClient *observer;

  /* Ongoing calls, as reffed TpChannels */
  GList *channels;
};

/* The Call Observer looks at incoming and outgoing calls, and
 * autorejects incoming ones if there are ongoing ones, since
 * we don't cope with simultaneous calls quite well yet.
 * At some point, we should ask the user if he wants to put the
 * current call on hold and answer the incoming one instead,
 * see https://bugzilla.gnome.org/show_bug.cgi?id=623348
 */
G_DEFINE_TYPE (EmpathyCallObserver, empathy_call_observer, G_TYPE_OBJECT);

static EmpathyCallObserver * observer_singleton = NULL;

static void
on_channel_closed (TpProxy *proxy,
    guint    domain,
    gint     code,
    gchar   *message,
    EmpathyCallObserver *self)
{
  self->priv->channels = g_list_remove (self->priv->channels, proxy);
  g_object_unref (proxy);
}

static void
on_cdo_claim_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  TpChannelDispatchOperation *cdo;
  TpChannel *channel = TP_CHANNEL (user_data);
  GError *error = NULL;

  cdo = TP_CHANNEL_DISPATCH_OPERATION (source_object);

  tp_channel_dispatch_operation_claim_finish (cdo, result, &error);
  if (error != NULL)
    {
      DEBUG ("Could not claim CDO: %s", error->message);
      g_error_free (error);
      return;
    }

  tp_channel_leave_async (channel,
      TP_CHANNEL_GROUP_CHANGE_REASON_BUSY, "Already in a call",
      NULL, NULL);
}

static TpChannel *
find_main_channel (GList *channels)
{
  GList *l;

  for (l = channels; l != NULL; l = g_list_next (l))
    {
      TpChannel *channel = l->data;
      GQuark channel_type;

      if (tp_proxy_get_invalidated (channel) != NULL)
        continue;

      channel_type = tp_channel_get_channel_type_id (channel);

      if (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_STREAMED_MEDIA ||
          channel_type == TPY_IFACE_QUARK_CHANNEL_TYPE_CALL)
        return channel;
    }

  return NULL;
}

static gboolean
has_ongoing_calls (EmpathyCallObserver *self)
{
  GList *l;

  for (l = self->priv->channels; l != NULL; l = l->next)
    {
      TpChannel *channel = TP_CHANNEL (l->data);
      GQuark type = tp_channel_get_channel_type_id (channel);

      /* Check that Call channels are not ended */
      if (type == TPY_IFACE_QUARK_CHANNEL_TYPE_CALL &&
          tpy_call_channel_get_state (TPY_CALL_CHANNEL (channel), NULL, NULL)
               == TPY_CALL_STATE_ENDED)
        continue;

      return TRUE;
    }

  return FALSE;
}

static void
observe_channels (TpSimpleObserver *observer,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    TpChannelDispatchOperation *dispatch_operation,
    GList *requests,
    TpObserveChannelsContext *context,
    gpointer user_data)
{
  EmpathyCallObserver *self = EMPATHY_CALL_OBSERVER (user_data);
  TpChannel *channel;
  const GError *error;

  channel = find_main_channel (channels);
  if (channel == NULL)
    {
      GError err = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Unknown channel type" };

      DEBUG ("Didn't find any Call or StreamedMedia channel; ignoring");

      tp_observe_channels_context_fail (context, &err);
      return;
    }

  /* Autoreject if there are other ongoing calls */
  if (has_ongoing_calls (self))
    {
      DEBUG ("Autorejecting incoming call since there are others in "
          "progress: %s", tp_proxy_get_object_path (channel));
      tp_channel_dispatch_operation_claim_async (dispatch_operation,
          on_cdo_claim_cb, g_object_ref (channel));
      return;
    }

  if ((error = tp_proxy_get_invalidated (channel)) != NULL)
    {
      DEBUG ("The channel has already been invalidated: %s",
          error->message);
      return;
    }

  tp_g_signal_connect_object (channel, "invalidated",
      G_CALLBACK (on_channel_closed), self, 0);
  self->priv->channels = g_list_prepend (self->priv->channels,
      g_object_ref (channel));

  tp_observe_channels_context_accept (context);
}

static GObject *
observer_constructor (GType type,
    guint n_props,
    GObjectConstructParam *props)
{
  GObject *retval;

  if (observer_singleton)
    {
      retval = g_object_ref (observer_singleton);
    }
  else
    {
      retval = G_OBJECT_CLASS (empathy_call_observer_parent_class)->constructor
          (type, n_props, props);

      observer_singleton = EMPATHY_CALL_OBSERVER (retval);
      g_object_add_weak_pointer (retval, (gpointer) &observer_singleton);
    }

  return retval;
}

static void
observer_dispose (GObject *object)
{
  EmpathyCallObserver *self = EMPATHY_CALL_OBSERVER (object);

  tp_clear_object (&self->priv->observer);
  g_list_free_full (self->priv->channels, g_object_unref);
  self->priv->channels = NULL;
}

static void
empathy_call_observer_class_init (EmpathyCallObserverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = observer_dispose;
  object_class->constructor = observer_constructor;

  g_type_class_add_private (object_class, sizeof (EmpathyCallObserverPriv));
}

static void
empathy_call_observer_init (EmpathyCallObserver *self)
{
  EmpathyCallObserverPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
    EMPATHY_TYPE_CALL_OBSERVER, EmpathyCallObserverPriv);
  TpDBusDaemon *dbus;
  EmpathyChannelFactory *factory;
  GError *error = NULL;

  self->priv = priv;

  dbus = tp_dbus_daemon_dup (&error);
  if (dbus == NULL)
    {
      DEBUG ("Failed to get TpDBusDaemon: %s", error->message);
      g_error_free (error);
      return;
    }

  self->priv->observer = tp_simple_observer_new (dbus, FALSE,
      "Empathy.CallObserver", FALSE,
      observe_channels, self, NULL);

  factory = empathy_channel_factory_dup ();
  tp_base_client_set_channel_factory (self->priv->observer,
      TP_CLIENT_CHANNEL_FACTORY (factory));
  g_object_unref (factory);

  /* Observe Call and StreamedMedia channels */
  tp_base_client_take_observer_filter (self->priv->observer,
      tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
          TP_HANDLE_TYPE_CONTACT,
        NULL));
  tp_base_client_take_observer_filter (self->priv->observer,
      tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TPY_IFACE_CHANNEL_TYPE_CALL,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
          TP_HANDLE_TYPE_CONTACT,
        NULL));

  if (!tp_base_client_register (self->priv->observer, &error))
    {
      DEBUG ("Failed to register observer: %s", error->message);
      g_error_free (error);
    }

  g_object_unref (dbus);
}

EmpathyCallObserver *
empathy_call_observer_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_CALL_OBSERVER, NULL);
}
