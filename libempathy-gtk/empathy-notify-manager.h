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

#ifndef __EMPATHY_NOTIFY_MANAGER_H__
#define __EMPATHY_NOTIFY_MANAGER_H__

#include <glib-object.h>

#include <libempathy/empathy-contact.h>

G_BEGIN_DECLS

#define EMPATHY_NOTIFY_MANAGER_CAP_ACTIONS             "actions"
#define EMPATHY_NOTIFY_MANAGER_CAP_BODY                "body"
#define EMPATHY_NOTIFY_MANAGER_CAP_BODY_HYPERLINKS     "body-hyperlinks"
#define EMPATHY_NOTIFY_MANAGER_CAP_BODY_IMAGES         "body-images"
#define EMPATHY_NOTIFY_MANAGER_CAP_BODY_MARKUP         "body-markup"
#define EMPATHY_NOTIFY_MANAGER_CAP_CATEGORY            "category"
#define EMPATHY_NOTIFY_MANAGER_CAP_ICON_MULTI          "icon-multi"
#define EMPATHY_NOTIFY_MANAGER_CAP_ICON_STATIC         "icon-static"
#define EMPATHY_NOTIFY_MANAGER_CAP_IMAGE_SVG_XML       "image/svg+xml"
#define EMPATHY_NOTIFY_MANAGER_CAP_SOUND                "sound"
/* notify-osd specific */
#define EMPATHY_NOTIFY_MANAGER_CAP_X_CANONICAL_APPEND              "x-canonical-append"
#define EMPATHY_NOTIFY_MANAGER_CAP_X_CANONICAL_PRIVATE_ICON_ONLY   "x-canonical-private-icon-only"
#define EMPATHY_NOTIFY_MANAGER_CAP_X_CANONICAL_PRIVATE_SYNCHRONOUS "x-canonical-private-synchronous"
#define EMPATHY_NOTIFY_MANAGER_CAP_X_CANONICAL_TRUNCATION          "x-canonical-truncation"

#define EMPATHY_TYPE_NOTIFY_MANAGER         (empathy_notify_manager_get_type ())
#define EMPATHY_NOTIFY_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_NOTIFY_MANAGER, EmpathyNotifyManager))
#define EMPATHY_NOTIFY_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_NOTIFY_MANAGER, EmpathyNotifyManagerClass))
#define EMPATHY_IS_NOTIFY_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_NOTIFY_MANAGER))
#define EMPATHY_IS_NOTIFY_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_NOTIFY_MANAGER))
#define EMPATHY_NOTIFY_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_NOTIFY_MANAGER, EmpathyNotifyManagerClass))

/* FIXME: this should *really* belong to libnotify. */
typedef enum {
    EMPATHY_NOTIFICATION_CLOSED_INVALID = 0,
    EMPATHY_NOTIFICATION_CLOSED_EXPIRED = 1,
    EMPATHY_NOTIFICATION_CLOSED_DISMISSED = 2,
    EMPATHY_NOTIFICATION_CLOSED_PROGRAMMATICALY = 3,
    EMPATHY_NOTIFICATION_CLOSED_RESERVED = 4
} EmpathyNotificationClosedReason;

typedef struct _EmpathyNotifyManager      EmpathyNotifyManager;
typedef struct _EmpathyNotifyManagerClass EmpathyNotifyManagerClass;

struct _EmpathyNotifyManager
{
  GObject parent;
  gpointer priv;
};

struct _EmpathyNotifyManagerClass
{
 GObjectClass parent_class;
};

GType empathy_notify_manager_get_type (void) G_GNUC_CONST;

/* Get the notify_manager singleton */
EmpathyNotifyManager * empathy_notify_manager_dup_singleton (void);

gboolean empathy_notify_manager_has_capability (EmpathyNotifyManager *self,
    const gchar *cap);

gboolean empathy_notify_manager_notification_is_enabled  (
    EmpathyNotifyManager *self);

GdkPixbuf * empathy_notify_manager_get_pixbuf_for_notification (
    EmpathyNotifyManager *self,
    EmpathyContact *contact,
    const char *icon_name);

G_END_DECLS

#endif /* __EMPATHY_NOTIFY_MANAGER_H__ */
