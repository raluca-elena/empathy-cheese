/* * Copyright (C) 2009 Collabora Ltd.
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
 * Authors: Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
 */

#include <config.h>
#include <string.h>

#include <libnotify/notification.h>
#include <libnotify/notify.h>
#include <telepathy-glib/telepathy-glib.h>

#include <libempathy-gtk/empathy-notify-manager.h>

#include "empathy-event-manager.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

#include "empathy-notifications-approver.h"

struct _EmpathyNotificationsApproverPrivate
{
  EmpathyEventManager *event_mgr;
  EmpathyNotifyManager *notify_mgr;
};

G_DEFINE_TYPE (EmpathyNotificationsApprover, empathy_notifications_approver,
    G_TYPE_OBJECT);

static EmpathyNotificationsApprover *notifications_approver = NULL;

static GObject *
notifications_approver_constructor (GType type,
    guint n_construct_params,
    GObjectConstructParam *construct_params)
{
  GObject *retval;

  if (notifications_approver != NULL)
    return g_object_ref (notifications_approver);

  retval = G_OBJECT_CLASS (empathy_notifications_approver_parent_class)->
      constructor (type, n_construct_params, construct_params);

  notifications_approver = EMPATHY_NOTIFICATIONS_APPROVER (retval);
  g_object_add_weak_pointer (retval, (gpointer) &notifications_approver);

  return retval;
}

static void
notifications_approver_dispose (GObject *object)
{
  EmpathyNotificationsApprover *self = (EmpathyNotificationsApprover *) object;

  tp_clear_object (&self->priv->event_mgr);
  tp_clear_object (&self->priv->notify_mgr);

  G_OBJECT_CLASS (empathy_notifications_approver_parent_class)->dispose (
      object);
}

static void
empathy_notifications_approver_class_init (
    EmpathyNotificationsApproverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = notifications_approver_dispose;
  object_class->constructor = notifications_approver_constructor;

  g_type_class_add_private (object_class,
      sizeof (EmpathyNotificationsApproverPrivate));
}

static void
empathy_notifications_approver_init (EmpathyNotificationsApprover *self)
{
  EmpathyNotificationsApproverPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
    EMPATHY_TYPE_NOTIFICATIONS_APPROVER, EmpathyNotificationsApproverPrivate);

  self->priv = priv;

  self->priv->event_mgr = empathy_event_manager_dup_singleton ();
  self->priv->notify_mgr = empathy_notify_manager_dup_singleton ();
}

EmpathyNotificationsApprover *
empathy_notifications_approver_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_NOTIFICATIONS_APPROVER, NULL);
}
