/*
 * Copyright (C) 2009 Collabora Ltd.
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

#ifndef __EMPATHY_NOTIFICATIONS_APPROVER_H__
#define __EMPATHY_NOTIFICATIONS_APPROVER_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_NOTIFICATIONS_APPROVER         (empathy_notifications_approver_get_type ())
#define EMPATHY_NOTIFICATIONS_APPROVER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_NOTIFICATIONS_APPROVER, EmpathyNotificationsApprover))
#define EMPATHY_NOTIFICATIONS_APPROVER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_NOTIFICATIONS_APPROVER, EmpathyNotificationsApproverClass))
#define EMPATHY_IS_NOTIFICATIONS_APPROVER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_NOTIFICATIONS_APPROVER))
#define EMPATHY_IS_NOTIFICATIONS_APPROVER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_NOTIFICATIONS_APPROVER))
#define EMPATHY_NOTIFICATIONS_APPROVER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_NOTIFICATIONS_APPROVER, EmpathyNotificationsApproverClass))

typedef struct _EmpathyNotificationsApprover      EmpathyNotificationsApprover;
typedef struct _EmpathyNotificationsApproverClass EmpathyNotificationsApproverClass;
typedef struct _EmpathyNotificationsApproverPrivate EmpathyNotificationsApproverPrivate;

struct _EmpathyNotificationsApprover
{
  GObject parent;
  EmpathyNotificationsApproverPrivate *priv;
};

struct _EmpathyNotificationsApproverClass
{
 GObjectClass parent_class;
};

GType empathy_notifications_approver_get_type (void) G_GNUC_CONST;

/* Get the notifications_approver singleton */
EmpathyNotificationsApprover * empathy_notifications_approver_dup_singleton (
    void);

G_END_DECLS

#endif /* __EMPATHY_NOTIFICATIONS_APPROVER_H__ */
