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

#include <glib/gi18n-lib.h>

#include <telepathy-glib/telepathy-glib.h>

#include <telepathy-yell/telepathy-yell.h>

#include <libnotify/notification.h>

#include <libempathy/empathy-channel-factory.h>
#include <libempathy/empathy-utils.h>

#include <libempathy-gtk/empathy-images.h>
#include <libempathy-gtk/empathy-notify-manager.h>

#include <extensions/extensions.h>

#include "empathy-call-observer.h"

#define DEBUG_FLAG EMPATHY_DEBUG_VOIP
#include <libempathy/empathy-debug.h>

struct _EmpathyCallObserverPriv {
  EmpathyNotifyManager *notify_mgr;

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
  DEBUG ("channel %s has been invalidated; stop observing it",
      tp_proxy_get_object_path (proxy));

  self->priv->channels = g_list_remove (self->priv->channels, proxy);
  g_object_unref (proxy);
}

typedef struct
{
  EmpathyCallObserver *self;
  TpObserveChannelsContext *context;
  TpChannel *main_channel;
} AutoRejectCtx;

static AutoRejectCtx *
auto_reject_ctx_new (EmpathyCallObserver *self,
    TpObserveChannelsContext *context,
    TpChannel *main_channel)
{
  AutoRejectCtx *ctx = g_slice_new (AutoRejectCtx);

  ctx->self = g_object_ref (self);
  ctx->context = g_object_ref (context);
  ctx->main_channel = g_object_ref (main_channel);
  return ctx;
}

static void
auto_reject_ctx_free (AutoRejectCtx *ctx)
{
  g_object_unref (ctx->self);
  g_object_unref (ctx->context);
  g_object_unref (ctx->main_channel);
  g_slice_free (AutoRejectCtx, ctx);
}

static void
get_contact_cb (TpConnection *connection,
    guint n_contacts,
    TpContact * const *contacts,
    guint n_failed,
    const TpHandle *failed,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyCallObserver *self = (EmpathyCallObserver *) weak_object;
  NotifyNotification *notification;
  TpContact *contact;
  gchar *summary, *body;
  EmpathyContact *emp_contact;
  GdkPixbuf *pixbuf;

  if (n_contacts != 1)
    {
      DEBUG ("Failed to get TpContact; ignoring notification bubble");
      return;
    }

  contact = contacts[0];

  summary = g_strdup_printf (_("Missed call from %s"),
      tp_contact_get_alias (contact));
  body = g_strdup_printf (
      _("%s just tried to call you, but you were in another call."),
      tp_contact_get_alias (contact));

  notification = notify_notification_new (summary, body, NULL);

  emp_contact = empathy_contact_dup_from_tp_contact (contact);
  pixbuf = empathy_notify_manager_get_pixbuf_for_notification (
      self->priv->notify_mgr, emp_contact, EMPATHY_IMAGE_AVATAR_DEFAULT);

  if (pixbuf != NULL)
    {
      notify_notification_set_icon_from_pixbuf (notification, pixbuf);
      g_object_unref (pixbuf);
    }

  notify_notification_show (notification, NULL);

  g_object_unref (notification);
  g_free (summary);
  g_free (body);
  g_object_unref (emp_contact);
}

static void
display_reject_notification (EmpathyCallObserver *self,
    TpChannel *channel)
{
  TpHandle handle;
  TpContactFeature features[] = { TP_CONTACT_FEATURE_ALIAS,
      TP_CONTACT_FEATURE_AVATAR_DATA };

  handle = tp_channel_get_handle (channel, NULL);

  tp_connection_get_contacts_by_handle (tp_channel_borrow_connection (channel),
      1, &handle, G_N_ELEMENTS (features), features, get_contact_cb,
      g_object_ref (channel), g_object_unref, G_OBJECT (self));
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
claim_and_leave_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  AutoRejectCtx *ctx = user_data;
  GError *error = NULL;

  if (!tp_channel_dispatch_operation_leave_channels_finish (
        TP_CHANNEL_DISPATCH_OPERATION (source), result, &error))
    {
      DEBUG ("Failed to reject call: %s", error->message);

      g_error_free (error);
      goto out;
    }

  display_reject_notification (ctx->self, ctx->main_channel);

out:
  auto_reject_ctx_free (ctx);
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
      AutoRejectCtx *ctx = auto_reject_ctx_new (self, context, channel);

      DEBUG ("Autorejecting incoming call since there are others in "
          "progress: %s", tp_proxy_get_object_path (channel));

      tp_channel_dispatch_operation_leave_channels_async (dispatch_operation,
          TP_CHANNEL_GROUP_CHANGE_REASON_BUSY, "Already in a call",
          claim_and_leave_cb, ctx);

      tp_observe_channels_context_accept (context);
      return;
    }

  if ((error = tp_proxy_get_invalidated (channel)) != NULL)
    {
      DEBUG ("The channel has already been invalidated: %s",
          error->message);

      tp_observe_channels_context_fail (context, error);
      return;
    }

  DEBUG ("Observing channel %s", tp_proxy_get_object_path (channel));

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

  tp_clear_object (&self->priv->notify_mgr);
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

  self->priv->notify_mgr = empathy_notify_manager_dup_singleton ();

  dbus = tp_dbus_daemon_dup (&error);
  if (dbus == NULL)
    {
      DEBUG ("Failed to get TpDBusDaemon: %s", error->message);
      g_error_free (error);
      return;
    }

  self->priv->observer = tp_simple_observer_new (dbus, TRUE,
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

  tp_base_client_set_observer_delay_approvers (self->priv->observer, TRUE);

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
