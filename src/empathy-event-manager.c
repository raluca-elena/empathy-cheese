/*
 * Copyright (C) 2007-2008 Collabora Ltd.
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
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Sjoerd Simons <sjoerd.simons@collabora.co.uk>
 */

#include <config.h>

#include <string.h>
#include <glib/gi18n.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/simple-approver.h>

#include <telepathy-yell/telepathy-yell.h>

#include <libempathy/empathy-presence-manager.h>
#include <libempathy/empathy-tp-contact-factory.h>
#include <libempathy/empathy-connection-aggregator.h>
#include <libempathy/empathy-tp-chat.h>
#include <libempathy/empathy-tp-streamed-media.h>
#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-gsettings.h>

#include <extensions/extensions.h>

#include <libempathy-gtk/empathy-images.h>
#include <libempathy-gtk/empathy-contact-dialogs.h>
#include <libempathy-gtk/empathy-sound-manager.h>
#include <libempathy-gtk/empathy-ui-utils.h>

#include "empathy-event-manager.h"
#include "empathy-main-window.h"

#define DEBUG_FLAG EMPATHY_DEBUG_DISPATCHER
#include <libempathy/empathy-debug.h>

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyEventManager)

#define NOTIFICATION_TIMEOUT 2 /* seconds */

#define ACCEPT_WITHOUT_VIDEO 1

/* The time interval in milliseconds between 2 incoming rings */
#define MS_BETWEEN_RING 500

typedef struct {
  EmpathyEventManager *manager;
  TpChannelDispatchOperation *operation;
  gulong invalidated_handler;
  /* Remove contact if applicable */
  EmpathyContact *contact;
  /* option signal handler and it's instance */
  gulong handler;
  GObject *handler_instance;
  /* optional accept widget */
  GtkWidget *dialog;
  /* Channel of the CDO that will be used during the approval */
  TpChannel *main_channel;
  gboolean auto_approved;
} EventManagerApproval;

typedef struct {
  TpBaseClient *approver;
  TpBaseClient *auth_approver;
  EmpathyConnectionAggregator *conn_aggregator;
  GSList *events;
  /* Ongoing approvals */
  GSList *approvals;

  gint ringing;

  GSettings *gsettings_notif;
  GSettings *gsettings_ui;

  EmpathySoundManager *sound_mgr;
} EmpathyEventManagerPriv;

typedef struct _EventPriv EventPriv;
typedef void (*EventFunc) (EventPriv *event);

struct _EventPriv {
  EmpathyEvent public;
  EmpathyEventManager *manager;
  EventManagerApproval *approval;
  EventFunc func;
  gboolean inhibit;
  gpointer user_data;
  guint autoremove_timeout_id;
};

enum {
  EVENT_ADDED,
  EVENT_REMOVED,
  EVENT_UPDATED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (EmpathyEventManager, empathy_event_manager, G_TYPE_OBJECT);

static EmpathyEventManager * manager_singleton = NULL;

static EventManagerApproval *
event_manager_approval_new (EmpathyEventManager *manager,
  TpChannelDispatchOperation *operation,
  TpChannel *main_channel)
{
  EventManagerApproval *result = g_slice_new0 (EventManagerApproval);
  result->operation = g_object_ref (operation);
  result->manager = manager;
  result->main_channel = g_object_ref (main_channel);

  return result;
}

static void
event_manager_approval_free (EventManagerApproval *approval)
{
  g_signal_handler_disconnect (approval->operation,
    approval->invalidated_handler);
  g_object_unref (approval->operation);

  g_object_unref (approval->main_channel);

  if (approval->handler != 0)
    g_signal_handler_disconnect (approval->handler_instance,
      approval->handler);

  if (approval->handler_instance != NULL)
    g_object_unref (approval->handler_instance);

  if (approval->contact != NULL)
    g_object_unref (approval->contact);

  if (approval->dialog != NULL)
    {
      gtk_widget_destroy (approval->dialog);
    }

  g_slice_free (EventManagerApproval, approval);
}

static void
event_free (EventPriv *event)
{
  g_free (event->public.icon_name);
  g_free (event->public.header);
  g_free (event->public.message);

  if (event->autoremove_timeout_id != 0)
    g_source_remove (event->autoremove_timeout_id);

  tp_clear_object (&(event->public.contact));
  tp_clear_object (&(event->public.account));

  g_slice_free (EventPriv, event);
}

static void
event_remove (EventPriv *event)
{
  EmpathyEventManagerPriv *priv = GET_PRIV (event->manager);

  DEBUG ("Removing event %p", event);

  priv->events = g_slist_remove (priv->events, event);
  g_signal_emit (event->manager, signals[EVENT_REMOVED], 0, event);
  event_free (event);
}

void
empathy_event_remove (EmpathyEvent *event_public)
{
  EventPriv *event = (EventPriv *) event_public;

  event_remove (event);
}

static gboolean
autoremove_event_timeout_cb (EventPriv *event)
{
  event->autoremove_timeout_id = 0;
  event_remove (event);
  return FALSE;
}

static gboolean
display_notify_area (EmpathyEventManager *self)
{
  EmpathyEventManagerPriv *priv = GET_PRIV (self);

  return g_settings_get_boolean (priv->gsettings_ui,
      EMPATHY_PREFS_UI_EVENTS_NOTIFY_AREA);
}

static void
event_manager_add (EmpathyEventManager *manager,
    TpAccount *account,
    EmpathyContact *contact,
    EmpathyEventType type,
    const gchar *icon_name,
    const gchar *header,
    const gchar *message,
    EventManagerApproval *approval,
    EventFunc func,
    gpointer user_data)
{
  EmpathyEventManagerPriv *priv = GET_PRIV (manager);
  EventPriv               *event;

  event = g_slice_new0 (EventPriv);
  event->public.account = account != NULL ? g_object_ref (account) : NULL;
  event->public.contact = contact ? g_object_ref (contact) : NULL;
  event->public.type = type;
  event->public.icon_name = g_strdup (icon_name);
  event->public.header = g_strdup (header);
  event->public.message = g_strdup (message);
  event->public.must_ack = (func != NULL);
  if (approval != NULL)
    event->public.handler_instance = approval->handler_instance;
  event->inhibit = FALSE;
  event->func = func;
  event->user_data = user_data;
  event->manager = manager;
  event->approval = approval;

  DEBUG ("Adding event %p", event);
  priv->events = g_slist_prepend (priv->events, event);

  if (!display_notify_area (manager))
    {
      /* Don't fire the 'event-added' signal as we activate the event now */
      if (approval != NULL)
        approval->auto_approved = TRUE;

      empathy_event_activate (&event->public);
      return;
    }

  g_signal_emit (event->manager, signals[EVENT_ADDED], 0, event);

  if (!event->public.must_ack)
    {
      event->autoremove_timeout_id = g_timeout_add_seconds (
          NOTIFICATION_TIMEOUT, (GSourceFunc) autoremove_event_timeout_cb,
          event);
    }
}

static void
handle_with_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  TpChannelDispatchOperation *cdo = TP_CHANNEL_DISPATCH_OPERATION (source);
  GError *error = NULL;

  if (!tp_channel_dispatch_operation_handle_with_finish (cdo, result, &error))
    {
      DEBUG ("HandleWith failed: %s\n", error->message);
      g_error_free (error);
    }
}

static void
handle_with_time_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  TpChannelDispatchOperation *cdo = TP_CHANNEL_DISPATCH_OPERATION (source);
  GError *error = NULL;

  if (!tp_channel_dispatch_operation_handle_with_time_finish (cdo, result,
        &error))
    {
      if (g_error_matches (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED))
        {
          EventManagerApproval *approval = user_data;

          DEBUG ("HandleWithTime() is not implemented, falling back to "
              "HandleWith(). Please upgrade to telepathy-mission-control "
              "5.5.0 or later");

          tp_channel_dispatch_operation_handle_with_async (approval->operation,
              NULL, handle_with_cb, approval);
        }
      else
        {
          DEBUG ("HandleWithTime failed: %s\n", error->message);
        }
      g_error_free (error);
    }
}

static void
event_manager_approval_approve (EventManagerApproval *approval)
{
  gint64 timestamp;

  if (approval->auto_approved)
    {
      timestamp = TP_USER_ACTION_TIME_NOT_USER_ACTION;
    }
  else
    {
      timestamp = empathy_get_current_action_time ();
    }

  g_assert (approval->operation != NULL);

  tp_channel_dispatch_operation_handle_with_time_async (approval->operation,
      NULL, timestamp, handle_with_time_cb, approval);
}

static void
event_channel_process_func (EventPriv *event)
{
  event_manager_approval_approve (event->approval);
}

static void
event_text_channel_process_func (EventPriv *event)
{
  EmpathyTpChat *tp_chat;

  if (event->approval->handler != 0)
    {
      tp_chat = EMPATHY_TP_CHAT (event->approval->handler_instance);

      g_signal_handler_disconnect (tp_chat, event->approval->handler);
      event->approval->handler = 0;
    }

  event_manager_approval_approve (event->approval);
}

static EventPriv *
event_lookup_by_approval (EmpathyEventManager *manager,
  EventManagerApproval *approval)
{
  EmpathyEventManagerPriv *priv = GET_PRIV (manager);
  GSList *l;
  EventPriv *retval = NULL;

  for (l = priv->events; l; l = l->next)
    {
      EventPriv *event = l->data;

      if (event->approval == approval)
        {
          retval = event;
          break;
        }
    }

  return retval;
}

static void
event_update (EmpathyEventManager *manager, EventPriv *event,
  const char *icon_name, const char *header, const char *msg)
{
  g_free (event->public.icon_name);
  g_free (event->public.header);
  g_free (event->public.message);

  event->public.icon_name = g_strdup (icon_name);
  event->public.header = g_strdup (header);
  event->public.message = g_strdup (msg);

  g_signal_emit (manager, signals[EVENT_UPDATED], 0, event);
}

static void
reject_channel_claim_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  TpChannelDispatchOperation *cdo = TP_CHANNEL_DISPATCH_OPERATION (source);
  GError *error = NULL;

  if (!tp_channel_dispatch_operation_claim_with_finish (cdo, result, &error))
    {
      DEBUG ("Failed to claim channel: %s", error->message);

      g_error_free (error);
      goto out;
    }

  if (EMPATHY_IS_TP_STREAMED_MEDIA (user_data))
    {
      empathy_tp_streamed_media_close (user_data);
    }
  else if (TPY_IS_CALL_CHANNEL (user_data))
    {
      tpy_call_channel_hangup_async (user_data,
          TPY_CALL_STATE_CHANGE_REASON_USER_REQUESTED,
          "", "", NULL, NULL);
      tp_channel_close_async (user_data, NULL, NULL);
    }
  else if (EMPATHY_IS_TP_CHAT (user_data))
    {
      empathy_tp_chat_leave (user_data, "");
    }
  else if (TP_IS_FILE_TRANSFER_CHANNEL (user_data))
    {
      tp_channel_close_async (user_data, NULL, NULL);
    }

out:
  g_object_unref (user_data);
}

static void
reject_approval (EventManagerApproval *approval)
{
  EmpathyEventManagerPriv *priv = GET_PRIV (approval->manager);

  /* We have to claim the channel before closing it */

  /* Unfortunately, we need to special case the auth channels for the
   * time being as they don't have a wrapper object handler in
   * approval->handler_instance as they're not actually handled by
   * this process, so we can just use a noddy callback to call Close()
   * directly. */
  if (approval->handler_instance != NULL)
    {
      tp_channel_dispatch_operation_claim_with_async (approval->operation,
          priv->approver, reject_channel_claim_cb,
          g_object_ref (approval->handler_instance));
    }
  else if (tp_channel_get_channel_type_id (approval->main_channel)
      == TP_IFACE_QUARK_CHANNEL_TYPE_SERVER_AUTHENTICATION)
    {
      tp_channel_dispatch_operation_close_channels_async (approval->operation,
          NULL, NULL);
    }
}

static void
event_manager_call_window_confirmation_dialog_response_cb (GtkDialog *dialog,
  gint response, gpointer user_data)
{
  EventManagerApproval *approval = user_data;

  gtk_widget_destroy (approval->dialog);
  approval->dialog = NULL;

  if (response == GTK_RESPONSE_ACCEPT)
    {
      event_manager_approval_approve (approval);
    }
  else if (response == ACCEPT_WITHOUT_VIDEO)
    {
      tpy_call_channel_send_video (TPY_CALL_CHANNEL (approval->main_channel),
        FALSE);
      event_manager_approval_approve (approval);
    }
  else
    {
      reject_approval (approval);
    }
}

static void
event_channel_process_voip_func (EventPriv *event)
{
  GtkWidget *dialog;
  GtkWidget *button;
  GtkWidget *image;
  gboolean video;
  gchar *title;
  EmpathyEventType etype = event->public.type;

  if (event->approval->dialog != NULL)
    {
      gtk_window_present (GTK_WINDOW (event->approval->dialog));
      return;
    }

  if (etype == EMPATHY_EVENT_TYPE_VOIP)
    {
      EmpathyTpStreamedMedia *call;
      call = EMPATHY_TP_STREAMED_MEDIA (event->approval->handler_instance);
      video = empathy_tp_streamed_media_has_initial_video (call);
    }
  else if (etype == EMPATHY_EVENT_TYPE_CALL)
    {
      TpyCallChannel *call;
      call = TPY_CALL_CHANNEL (event->approval->handler_instance);
      g_object_get (G_OBJECT (call), "initial-video", &video, NULL);
    }
  else
    {
      g_warning ("Unknown event type: %d", event->public.type);
      return;
    }

  dialog = gtk_message_dialog_new (NULL, 0,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
      video ? _("Incoming video call"): _("Incoming call"));

  gtk_message_dialog_format_secondary_text (
    GTK_MESSAGE_DIALOG (dialog), video ?
      _("%s is video calling you. Do you want to answer?"):
      _("%s is calling you. Do you want to answer?"),
      empathy_contact_get_alias (event->approval->contact));

  title = g_strdup_printf (_("Incoming call from %s"),
      empathy_contact_get_alias (event->approval->contact));

  gtk_window_set_title (GTK_WINDOW (dialog), title);
  g_free (title);

  /* Set image of the dialog */
  if (video)
    {
      image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_VIDEO_CALL,
          GTK_ICON_SIZE_DIALOG);
    }
  else
    {
      image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_VOIP,
          GTK_ICON_SIZE_DIALOG);
    }

  gtk_message_dialog_set_image (GTK_MESSAGE_DIALOG (dialog), image);
  gtk_widget_show (image);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog),
      GTK_RESPONSE_OK);

  button = gtk_dialog_add_button (GTK_DIALOG (dialog),
      _("_Reject"), GTK_RESPONSE_REJECT);
  image = gtk_image_new_from_icon_name ("call-stop",
    GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (button), image);

  if (video && etype == EMPATHY_EVENT_TYPE_CALL)
    {
      button = gtk_dialog_add_button (GTK_DIALOG (dialog),
        _("_Answer"), ACCEPT_WITHOUT_VIDEO);

      image = gtk_image_new_from_icon_name ("call-start",
        GTK_ICON_SIZE_BUTTON);
      gtk_button_set_image (GTK_BUTTON (button), image);
    }

  button = gtk_dialog_add_button (GTK_DIALOG (dialog),
    video ? _("_Answer with video") : _("_Answer"), GTK_RESPONSE_ACCEPT);

  image = gtk_image_new_from_icon_name ("call-start",
        GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (button), image);

  g_signal_connect (dialog, "response",
    G_CALLBACK (event_manager_call_window_confirmation_dialog_response_cb),
    event->approval);

  gtk_widget_show (dialog);

  event->approval->dialog = dialog;
}

static void
event_manager_chat_message_received_cb (EmpathyTpChat *tp_chat,
  EmpathyMessage *message,
  EventManagerApproval *approval)
{
  GtkWidget       *window;
  EmpathyContact  *sender;
  const gchar     *header;
  const gchar     *msg;
  EventPriv       *event;
  EmpathyEventManagerPriv *priv = GET_PRIV (approval->manager);

  /* try to update the event if it's referring to a chat which is already in the
   * queue. */
  event = event_lookup_by_approval (approval->manager, approval);

  sender = empathy_message_get_sender (message);

  /* We only want to show incoming messages */
  if (empathy_contact_is_user (sender))
    return;

  header = empathy_contact_get_alias (sender);
  msg = empathy_message_get_body (message);

  if (event != NULL)
    event_update (approval->manager, event, EMPATHY_IMAGE_NEW_MESSAGE, header,
        msg);
  else
    event_manager_add (approval->manager, NULL, sender,
        EMPATHY_EVENT_TYPE_CHAT, EMPATHY_IMAGE_NEW_MESSAGE, header, msg,
        approval, event_text_channel_process_func, NULL);

  window = empathy_main_window_dup ();

  empathy_sound_manager_play (priv->sound_mgr, window,
      EMPATHY_SOUND_CONVERSATION_NEW);

  g_object_unref (window);
}

static void
event_manager_approval_done (EventManagerApproval *approval)
{
  EmpathyEventManagerPriv *priv = GET_PRIV (approval->manager);
  GSList                  *l;

  if (approval->operation != NULL)
    {
      GQuark channel_type;

      channel_type = tp_channel_get_channel_type_id (approval->main_channel);

      if (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_STREAMED_MEDIA ||
          channel_type == TPY_IFACE_QUARK_CHANNEL_TYPE_CALL)
        {
          priv->ringing--;
          if (priv->ringing == 0)
            empathy_sound_manager_stop (priv->sound_mgr,
                EMPATHY_SOUND_PHONE_INCOMING);
        }
    }

  priv->approvals = g_slist_remove (priv->approvals, approval);

  for (l = priv->events; l; l = l->next)
    {
      EventPriv *event = l->data;

      if (event->approval == approval)
        {
          event_remove (event);
          break;
        }
    }

  event_manager_approval_free (approval);
}

static void
cdo_invalidated_cb (TpProxy *cdo,
    guint domain,
    gint code,
    gchar *message,
    EventManagerApproval *approval)
{
  DEBUG ("ChannelDispatchOperation has been invalidated: %s", message);

  event_manager_approval_done (approval);
}

static void
event_manager_call_state_changed_cb (TpyCallChannel *call,
  TpyCallState state,
  TpyCallFlags flags,
   const GValueArray *call_state_reason,
  GHashTable *call_state_details,
  EventManagerApproval *approval)
{
  if (state == TPY_CALL_STATE_ENDED)
    {
      DEBUG ("Call ended, seems we missed it :/");
      reject_approval (approval);
    }
}

static void
event_manager_call_channel_got_contact_cb (TpConnection *connection,
                                 EmpathyContact *contact,
                                 const GError *error,
                                 gpointer user_data,
                                 GObject *object)
{
  EventManagerApproval *approval = (EventManagerApproval *) user_data;
  EmpathyEventManagerPriv *priv = GET_PRIV (approval->manager);
  GtkWidget *window;
  TpyCallChannel *call;
  gchar *header;
  gboolean video;

  call = TPY_CALL_CHANNEL (approval->handler_instance);

  if (error != NULL)
    {
      DEBUG ("Can't get the contact for the call.. Rejecting?");
      reject_approval (approval);
      return;
    }

  if (tpy_call_channel_get_state (call, NULL, NULL) == TPY_CALL_STATE_ENDED)
    {
      DEBUG ("Call already ended, seems we missed it :/");
      reject_approval (approval);
      return;
    }

  approval->handler = g_signal_connect (call, "state-changed",
    G_CALLBACK (event_manager_call_state_changed_cb), approval);

  window = empathy_main_window_dup ();
  approval->contact = g_object_ref (contact);

  g_object_get (G_OBJECT (call), "initial-video", &video, NULL);

  header = g_strdup_printf (
    video ? _("Incoming video call from %s") :_("Incoming call from %s"),
    empathy_contact_get_alias (approval->contact));

  event_manager_add (approval->manager, NULL,
      approval->contact, EMPATHY_EVENT_TYPE_CALL,
      video ? EMPATHY_IMAGE_VIDEO_CALL : EMPATHY_IMAGE_VOIP,
      header, NULL, approval,
      event_channel_process_voip_func, NULL);

  g_free (header);

  priv->ringing++;
  if (priv->ringing == 1)
    empathy_sound_manager_start_playing (priv->sound_mgr, window,
        EMPATHY_SOUND_PHONE_INCOMING, MS_BETWEEN_RING);

  g_object_unref (window);
}

static void
event_manager_media_channel_got_contact (EventManagerApproval *approval)
{
  EmpathyEventManagerPriv *priv = GET_PRIV (approval->manager);
  GtkWidget *window = empathy_main_window_dup ();
  gchar *header;
  EmpathyTpStreamedMedia *call;
  gboolean video;

  call = EMPATHY_TP_STREAMED_MEDIA (approval->handler_instance);

  video = empathy_tp_streamed_media_has_initial_video (call);

  header = g_strdup_printf (
    video ? _("Incoming video call from %s") :_("Incoming call from %s"),
    empathy_contact_get_alias (approval->contact));

  event_manager_add (approval->manager, NULL,
      approval->contact, EMPATHY_EVENT_TYPE_VOIP,
      video ? EMPATHY_IMAGE_VIDEO_CALL : EMPATHY_IMAGE_VOIP,
      header, NULL, approval,
      event_channel_process_voip_func, NULL);

  g_free (header);

  priv->ringing++;
  if (priv->ringing == 1)
    empathy_sound_manager_start_playing (priv->sound_mgr, window,
        EMPATHY_SOUND_PHONE_INCOMING, MS_BETWEEN_RING);

  g_object_unref (window);
}

static void
event_manager_media_channel_contact_changed_cb (EmpathyTpStreamedMedia *call,
  GParamSpec *param, EventManagerApproval *approval)
{
  EmpathyContact *contact;

  g_object_get (G_OBJECT (call), "contact", &contact, NULL);

  if (contact == NULL)
    return;

  approval->contact = contact;
  event_manager_media_channel_got_contact (approval);
}

static void
invite_dialog_response_cb (GtkDialog *dialog,
                           gint response,
                           EventManagerApproval *approval)
{
  gtk_widget_destroy (GTK_WIDGET (approval->dialog));
  approval->dialog = NULL;

  if (response != GTK_RESPONSE_OK)
    {
      /* close channel */
      DEBUG ("Muc invitation rejected");

      reject_approval (approval);

      return;
    }

  DEBUG ("Muc invitation accepted");

  /* We'll join the room when handling the channel */
  event_manager_approval_approve (approval);
}

static void
event_room_channel_process_func (EventPriv *event)
{
  GtkWidget *dialog, *button, *image;
  TpChannel *channel = event->approval->main_channel;
  gchar *title;

  if (event->approval->dialog != NULL)
    {
      gtk_window_present (GTK_WINDOW (event->approval->dialog));
      return;
    }

  /* create dialog */
  dialog = gtk_message_dialog_new (NULL, 0,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, _("Room invitation"));

  title = g_strdup_printf (_("Invitation to join %s"),
      tp_channel_get_identifier (channel));

  gtk_window_set_title (GTK_WINDOW (dialog), title);
  g_free (title);

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
      _("%s is inviting you to join %s"),
      empathy_contact_get_alias (event->approval->contact),
      tp_channel_get_identifier (channel));

  gtk_dialog_set_default_response (GTK_DIALOG (dialog),
      GTK_RESPONSE_OK);

  button = gtk_dialog_add_button (GTK_DIALOG (dialog),
      _("_Decline"), GTK_RESPONSE_CANCEL);
  image = gtk_image_new_from_icon_name (GTK_STOCK_CANCEL, GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (button), image);

  button = gtk_dialog_add_button (GTK_DIALOG (dialog),
      _("_Join"), GTK_RESPONSE_OK);
  image = gtk_image_new_from_icon_name (GTK_STOCK_APPLY, GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (button), image);

  g_signal_connect (dialog, "response",
      G_CALLBACK (invite_dialog_response_cb), event->approval);

  gtk_widget_show (dialog);

  event->approval->dialog = dialog;
}

static void
display_invite_room_dialog (EventManagerApproval *approval)
{
  GtkWidget *window = empathy_main_window_dup ();
  const gchar *invite_msg;
  gchar *msg;
  TpHandle self_handle;
  EmpathyEventManagerPriv *priv = GET_PRIV (approval->manager);

  self_handle = tp_channel_group_get_self_handle (approval->main_channel);
  tp_channel_group_get_local_pending_info (approval->main_channel, self_handle,
      NULL, NULL, &invite_msg);

  if (approval->contact != NULL)
    {
      msg = g_strdup_printf (_("%s invited you to join %s"),
          empathy_contact_get_alias (approval->contact),
          tp_channel_get_identifier (approval->main_channel));
    }
  else
    {
      msg = g_strdup_printf (_("You have been invited to join %s"),
          tp_channel_get_identifier (approval->main_channel));
    }

  event_manager_add (approval->manager, NULL,
      approval->contact, EMPATHY_EVENT_TYPE_INVITATION,
      EMPATHY_IMAGE_GROUP_MESSAGE, msg, invite_msg, approval,
      event_room_channel_process_func, NULL);

  empathy_sound_manager_play (priv->sound_mgr, window,
      EMPATHY_SOUND_CONVERSATION_NEW);

  g_free (msg);
  g_object_unref (window);
}

static void
event_manager_muc_invite_got_contact_cb (TpConnection *connection,
                                         EmpathyContact *contact,
                                         const GError *error,
                                         gpointer user_data,
                                         GObject *object)
{
  EventManagerApproval *approval = (EventManagerApproval *) user_data;

  if (error != NULL)
    {
      DEBUG ("Error: %s", error->message);
    }
  else
    {
      approval->contact = g_object_ref (contact);
    }

  display_invite_room_dialog (approval);
}

static void
event_manager_ft_got_contact_cb (TpConnection *connection,
                                 EmpathyContact *contact,
                                 const GError *error,
                                 gpointer user_data,
                                 GObject *object)
{
  EventManagerApproval *approval = (EventManagerApproval *) user_data;
  GtkWidget *window = empathy_main_window_dup ();
  char *header;
  EmpathyEventManagerPriv *priv = GET_PRIV (approval->manager);

  approval->contact = g_object_ref (contact);

  header = g_strdup_printf (_("Incoming file transfer from %s"),
                            empathy_contact_get_alias (approval->contact));

  event_manager_add (approval->manager, NULL,
      approval->contact, EMPATHY_EVENT_TYPE_TRANSFER,
      EMPATHY_IMAGE_DOCUMENT_SEND, header, NULL,
      approval, event_channel_process_func, NULL);

  /* FIXME better sound for incoming file transfers ?*/
  empathy_sound_manager_play (priv->sound_mgr, window,
      EMPATHY_SOUND_CONVERSATION_NEW);

  g_free (header);
  g_object_unref (window);
}

static void
event_manager_auth_process_func (EventPriv *event)
{
  empathy_event_approve ((EmpathyEvent *) event);
}

/* If there is a file-transfer, media, or auth channel consider it as
 * the main one. */
static TpChannel *
find_main_channel (GList *channels)
{
  GList *l;
  TpChannel *text = NULL;

  for (l = channels; l != NULL; l = g_list_next (l))
    {
      TpChannel *channel = l->data;
      GQuark channel_type;

      if (tp_proxy_get_invalidated (channel) != NULL)
        continue;

      channel_type = tp_channel_get_channel_type_id (channel);

      if (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_STREAMED_MEDIA ||
          channel_type == TPY_IFACE_QUARK_CHANNEL_TYPE_CALL ||
          channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_FILE_TRANSFER ||
          channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_SERVER_AUTHENTICATION)
        return channel;

      else if (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_TEXT)
        text = channel;
    }

  return text;
}

static void
approve_channels (TpSimpleApprover *approver,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    TpChannelDispatchOperation *dispatch_operation,
    TpAddDispatchOperationContext *context,
    gpointer user_data)
{
  EmpathyEventManager *self = user_data;
  EmpathyEventManagerPriv *priv = GET_PRIV (self);
  TpChannel *channel;
  EventManagerApproval *approval;
  GQuark channel_type;

  channel = find_main_channel (channels);
  if (channel == NULL)
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Unknown channel type" };

      DEBUG ("Failed to find the main channel; ignoring");

      tp_add_dispatch_operation_context_fail (context, &error);
      return;
    }

  approval = event_manager_approval_new (self, dispatch_operation, channel);
  priv->approvals = g_slist_prepend (priv->approvals, approval);

  approval->invalidated_handler = g_signal_connect (dispatch_operation,
      "invalidated", G_CALLBACK (cdo_invalidated_cb), approval);

  channel_type = tp_channel_get_channel_type_id (channel);

  if (TP_IS_TEXT_CHANNEL (channel))
    {
      EmpathyTpChat *tp_chat = EMPATHY_TP_CHAT (channel);
      GList *messages, *l;

      approval->handler_instance = g_object_ref (tp_chat);

      if (tp_proxy_has_interface (channel, TP_IFACE_CHANNEL_INTERFACE_GROUP))
        {
          /* Are we in local-pending ? */
          TpHandle inviter;

          if (empathy_tp_chat_is_invited (tp_chat, &inviter))
            {
              /* We are invited to a room */
              DEBUG ("Have been invited to %s. Ask user if he wants to accept",
                  tp_channel_get_identifier (channel));

              if (inviter != 0)
                {
                  empathy_tp_contact_factory_get_from_handle (connection,
                      inviter, event_manager_muc_invite_got_contact_cb,
                      approval, NULL, G_OBJECT (self));
                }
              else
                {
                  display_invite_room_dialog (approval);
                }

              goto out;
            }

          /* We are not invited, approve the channel right now */
          tp_add_dispatch_operation_context_accept (context);

          approval->auto_approved = TRUE;
          event_manager_approval_approve (approval);
          return;
        }

      /* 1-1 text channel, wait for the first message */
      approval->handler = g_signal_connect (tp_chat, "message-received-empathy",
        G_CALLBACK (event_manager_chat_message_received_cb), approval);

      messages = (GList *) empathy_tp_chat_get_pending_messages (tp_chat);
      for (l = messages; l != NULL; l = g_list_next (l))
        {
          EmpathyMessage *msg = l->data;

          event_manager_chat_message_received_cb (tp_chat, msg, approval);
        }
    }
  else if (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_STREAMED_MEDIA)
    {
      EmpathyContact *contact;
      EmpathyTpStreamedMedia *call = empathy_tp_streamed_media_new (account,
        channel);

      approval->handler_instance = G_OBJECT (call);

      g_object_get (G_OBJECT (call), "contact", &contact, NULL);

      if (contact == NULL)
        {
          g_signal_connect (call, "notify::contact",
            G_CALLBACK (event_manager_media_channel_contact_changed_cb),
            approval);
        }
      else
        {
          approval->contact = contact;
          event_manager_media_channel_got_contact (approval);
        }

    }
  else if (channel_type == TPY_IFACE_QUARK_CHANNEL_TYPE_CALL)
    {
      TpyCallChannel *call = TPY_CALL_CHANNEL (channel);
      const gchar *id;

      approval->handler_instance = g_object_ref (call);

      id = tp_channel_get_identifier (channel);

      empathy_tp_contact_factory_get_from_id (connection, id,
        event_manager_call_channel_got_contact_cb,
        approval, NULL, G_OBJECT (self));
    }
  else if (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_FILE_TRANSFER)
    {
      TpHandle handle;

      approval->handler_instance = g_object_ref (channel);

      handle = tp_channel_get_handle (channel, NULL);

      empathy_tp_contact_factory_get_from_handle (connection, handle,
        event_manager_ft_got_contact_cb, approval, NULL, G_OBJECT (self));
    }
  else if (channel_type == TP_IFACE_QUARK_CHANNEL_TYPE_SERVER_AUTHENTICATION)
    {
      GHashTable *props;
      const gchar * const *available_mechanisms;

      props = tp_channel_borrow_immutable_properties (channel);
      available_mechanisms = tp_asv_get_boxed (props,
          TP_PROP_CHANNEL_INTERFACE_SASL_AUTHENTICATION_AVAILABLE_MECHANISMS,
          G_TYPE_STRV);

      if (tp_strv_contains (available_mechanisms, "X-TELEPATHY-PASSWORD"))
        {
          event_manager_add (approval->manager, account, NULL,
              EMPATHY_EVENT_TYPE_AUTH,
              GTK_STOCK_DIALOG_AUTHENTICATION,
              tp_account_get_display_name (account),
              _("Password required"), approval,
              event_manager_auth_process_func, NULL);
        }
      else
        {
          GError error = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
              "Support only X-TELEPATHY-PASSWORD auth method" };

          tp_add_dispatch_operation_context_fail (context, &error);
          return;
        }
    }
  else
    {
      GError error = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Invalid channel type" };

      DEBUG ("Unknown channel type (%s), ignoring..",
          g_quark_to_string (channel_type));

      tp_add_dispatch_operation_context_fail (context, &error);
      return;
    }

out:
  tp_add_dispatch_operation_context_accept (context);
}

#if 0
static void
event_pending_subscribe_func (EventPriv *event)
{
  empathy_subscription_dialog_show (event->public.contact, event->public.header,
      NULL);
  event_remove (event);
}

static void
event_manager_pendings_changed_cb (EmpathyContactList  *list,
  EmpathyContact *contact, EmpathyContact *actor,
  guint reason, gchar *message, gboolean is_pending,
  EmpathyEventManager *manager)
{
  EmpathyEventManagerPriv *priv = GET_PRIV (manager);
  gchar                   *header, *event_msg;

  if (!is_pending)
    {
      GSList *l;

      for (l = priv->events; l; l = l->next)
        {
          EventPriv *event = l->data;

          if (event->public.contact == contact &&
              event->func == event_pending_subscribe_func)
            {
              event_remove (event);
              break;
            }
        }

      return;
    }

  header = g_strdup_printf (
      _("%s would like permission to see when you are online"),
      empathy_contact_get_alias (contact));

  if (!EMP_STR_EMPTY (message))
    event_msg = g_strdup_printf (_("\nMessage: %s"), message);
  else
    event_msg = NULL;

  event_manager_add (manager, NULL, contact, EMPATHY_EVENT_TYPE_SUBSCRIPTION,
      GTK_STOCK_DIALOG_QUESTION, header, event_msg, NULL,
      event_pending_subscribe_func, NULL);

  g_free (event_msg);
  g_free (header);
}

static void
event_manager_presence_changed_cb (EmpathyContact *contact,
    TpConnectionPresenceType current,
    TpConnectionPresenceType previous,
    EmpathyEventManager *manager)
{
  EmpathyEventManagerPriv *priv = GET_PRIV (manager);
  TpAccount *account;
  EmpathyPresenceManager *presence_mgr;
  GtkWidget *window = empathy_main_window_dup ();

  account = empathy_contact_get_account (contact);
  presence_mgr = empathy_presence_manager_dup_singleton ();

  if (empathy_presence_manager_account_is_just_connected (presence_mgr, account))
    goto out;

  if (tp_connection_presence_type_cmp_availability (previous,
        TP_CONNECTION_PRESENCE_TYPE_OFFLINE) > 0)
    {
      /* contact was online */
      if (tp_connection_presence_type_cmp_availability (current,
          TP_CONNECTION_PRESENCE_TYPE_OFFLINE) <= 0)
        {
          /* someone is logging off */
          empathy_sound_manager_play (priv->sound_mgr, window,
              EMPATHY_SOUND_CONTACT_DISCONNECTED);

          if (g_settings_get_boolean (priv->gsettings_notif,
                EMPATHY_PREFS_NOTIFICATIONS_CONTACT_SIGNOUT))
            {
              event_manager_add (manager, NULL, contact,
                  EMPATHY_EVENT_TYPE_PRESENCE_OFFLINE,
                  EMPATHY_IMAGE_AVATAR_DEFAULT,
                  empathy_contact_get_alias (contact), _("Disconnected"),
                  NULL, NULL, NULL);
            }
        }
    }
  else
    {
      /* contact was offline */
      if (tp_connection_presence_type_cmp_availability (current,
            TP_CONNECTION_PRESENCE_TYPE_OFFLINE) > 0)
        {
          /* someone is logging in */
          empathy_sound_manager_play (priv->sound_mgr, window,
              EMPATHY_SOUND_CONTACT_CONNECTED);

          if (g_settings_get_boolean (priv->gsettings_notif,
                EMPATHY_PREFS_NOTIFICATIONS_CONTACT_SIGNIN))
            {
              event_manager_add (manager, NULL, contact,
                  EMPATHY_EVENT_TYPE_PRESENCE_ONLINE,
                  EMPATHY_IMAGE_AVATAR_DEFAULT,
                  empathy_contact_get_alias (contact), _("Connected"),
                  NULL, NULL, NULL);
            }
        }
    }

out:
  g_object_unref (presence_mgr);
  g_object_unref (window);
}

static void
event_manager_members_changed_cb (EmpathyContactList  *list,
    EmpathyContact *contact,
    EmpathyContact *actor,
    guint reason,
    gchar *message,
    gboolean is_member,
    EmpathyEventManager *manager)
{
  if (is_member)
    g_signal_connect (contact, "presence-changed",
        G_CALLBACK (event_manager_presence_changed_cb), manager);
  else
    g_signal_handlers_disconnect_by_func (contact,
        event_manager_presence_changed_cb, manager);
}
#endif

static GObject *
event_manager_constructor (GType type,
			   guint n_props,
			   GObjectConstructParam *props)
{
	GObject *retval;

	if (manager_singleton) {
		retval = g_object_ref (manager_singleton);
	} else {
		retval = G_OBJECT_CLASS (empathy_event_manager_parent_class)->constructor
			(type, n_props, props);

		manager_singleton = EMPATHY_EVENT_MANAGER (retval);
		g_object_add_weak_pointer (retval, (gpointer) &manager_singleton);
	}

	return retval;
}

static void
event_manager_finalize (GObject *object)
{
  EmpathyEventManagerPriv *priv = GET_PRIV (object);

  if (priv->ringing > 0)
    empathy_sound_manager_stop (priv->sound_mgr, EMPATHY_SOUND_PHONE_INCOMING);

  g_slist_foreach (priv->events, (GFunc) event_free, NULL);
  g_slist_free (priv->events);
  g_slist_foreach (priv->approvals, (GFunc) event_manager_approval_free, NULL);
  g_slist_free (priv->approvals);
  g_object_unref (priv->conn_aggregator);
  g_object_unref (priv->approver);
  g_object_unref (priv->auth_approver);
  g_object_unref (priv->gsettings_notif);
  g_object_unref (priv->gsettings_ui);
  g_object_unref (priv->sound_mgr);
}

static void
empathy_event_manager_class_init (EmpathyEventManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = event_manager_finalize;
  object_class->constructor = event_manager_constructor;

  signals[EVENT_ADDED] =
    g_signal_new ("event-added",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_generic,
      G_TYPE_NONE,
      1, G_TYPE_POINTER);

  signals[EVENT_REMOVED] =
  g_signal_new ("event-removed",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, G_TYPE_POINTER);

  signals[EVENT_UPDATED] =
  g_signal_new ("event-updated",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, G_TYPE_POINTER);

  g_type_class_add_private (object_class, sizeof (EmpathyEventManagerPriv));
}

static void
contact_list_changed_cb (EmpathyConnectionAggregator *aggregator,
    TpConnection *conn,
    GPtrArray *added,
    GPtrArray *removed,
    EmpathyEventManager *self)
{
  g_print ("%u added; %u removed on %s\n", added->len, removed->len, tp_proxy_get_object_path (conn));
}

static void
empathy_event_manager_init (EmpathyEventManager *manager)
{
  EmpathyEventManagerPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (manager,
    EMPATHY_TYPE_EVENT_MANAGER, EmpathyEventManagerPriv);
  GError *error = NULL;
  TpAccountManager *am;
  GPtrArray *contacts;

  manager->priv = priv;

  priv->gsettings_notif = g_settings_new (EMPATHY_PREFS_NOTIFICATIONS_SCHEMA);
  priv->gsettings_ui = g_settings_new (EMPATHY_PREFS_UI_SCHEMA);

  priv->sound_mgr = empathy_sound_manager_dup_singleton ();

  priv->conn_aggregator = empathy_connection_aggregator_dup_singleton ();

  tp_g_signal_connect_object (priv->conn_aggregator, "contact-list-changed",
      G_CALLBACK (contact_list_changed_cb), manager, 0);

#if 0
  g_signal_connect (priv->contact_manager, "pendings-changed",
    G_CALLBACK (event_manager_pendings_changed_cb), manager);

  g_signal_connect (priv->contact_manager, "members-changed",
    G_CALLBACK (event_manager_members_changed_cb), manager);
#endif

  contacts = empathy_connection_aggregator_dup_all_contacts (
      priv->conn_aggregator);

  g_print ("XXXXXXXXXXXXXXXx %d contacts\n", contacts->len);

  g_ptr_array_unref (contacts);

   am = tp_account_manager_dup ();

  priv->approver = tp_simple_approver_new_with_am (am, "Empathy.EventManager",
      FALSE, approve_channels, manager, NULL);

  /* Private text channels */
  tp_base_client_take_approver_filter (priv->approver,
      tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_TEXT,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
        NULL));

  /* Muc text channels */
  tp_base_client_take_approver_filter (priv->approver,
      tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_TEXT,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_ROOM,
        NULL));

  /* File transfer */
  tp_base_client_take_approver_filter (priv->approver,
      tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
        NULL));

  /* Calls */
  tp_base_client_take_approver_filter (priv->approver,
      tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
        NULL));
  tp_base_client_take_approver_filter (priv->approver,
      tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TPY_IFACE_CHANNEL_TYPE_CALL,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
        NULL));

  /* I don't feel good about doing this, and I'm sorry, but the
   * capabilities connection feature is added earlier because it's
   * needed for EmpathyTpChat. If the capabilities feature is required
   * then preparing an auth channel (which of course appears in the
   * CONNECTING state) will never be prepared. So the options are
   * either to create another approver like I've done, or to port
   * EmpathyTpChat and its users to not depend on the connection being
   * prepared with capabilities. I chose the former, obviously. :-) */

  priv->auth_approver = tp_simple_approver_new_with_am (am,
      "Empathy.AuthEventManager", FALSE, approve_channels, manager,
      NULL);

  /* SASL auth channels */
  tp_base_client_take_approver_filter (priv->auth_approver,
      tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_SERVER_AUTHENTICATION,
        TP_PROP_CHANNEL_TYPE_SERVER_AUTHENTICATION_AUTHENTICATION_METHOD,
          G_TYPE_STRING,
          TP_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
        NULL));

  if (!tp_base_client_register (priv->approver, &error))
    {
      DEBUG ("Failed to register Approver: %s", error->message);
      g_error_free (error);
    }

  if (!tp_base_client_register (priv->auth_approver, &error))
    {
      DEBUG ("Failed to register auth Approver: %s", error->message);
      g_error_free (error);
    }

  g_object_unref (am);
}

EmpathyEventManager *
empathy_event_manager_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_EVENT_MANAGER, NULL);
}

GSList *
empathy_event_manager_get_events (EmpathyEventManager *manager)
{
  EmpathyEventManagerPriv *priv = GET_PRIV (manager);

  g_return_val_if_fail (EMPATHY_IS_EVENT_MANAGER (manager), NULL);

  return priv->events;
}

EmpathyEvent *
empathy_event_manager_get_top_event (EmpathyEventManager *manager)
{
  EmpathyEventManagerPriv *priv = GET_PRIV (manager);

  g_return_val_if_fail (EMPATHY_IS_EVENT_MANAGER (manager), NULL);

  return priv->events ? priv->events->data : NULL;
}

void
empathy_event_activate (EmpathyEvent *event_public)
{
  EventPriv *event = (EventPriv *) event_public;

  g_return_if_fail (event_public != NULL);

  if (event->func)
    event->func (event);
  else
    event_remove (event);
}

void
empathy_event_inhibit_updates (EmpathyEvent *event_public)
{
  EventPriv *event = (EventPriv *) event_public;

  g_return_if_fail (event_public != NULL);

  event->inhibit = TRUE;
}

void
empathy_event_approve (EmpathyEvent *event_public)
{
  EventPriv *event = (EventPriv *) event_public;

  g_return_if_fail (event_public != NULL);

  event_manager_approval_approve (event->approval);
}

void
empathy_event_decline (EmpathyEvent *event_public)
{
  EventPriv *event = (EventPriv *) event_public;

  g_return_if_fail (event_public != NULL);

  reject_approval (event->approval);
}
