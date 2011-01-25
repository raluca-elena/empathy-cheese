/*
 * Copyright (C) 2007-2011 Collabora Ltd.
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
 */

#ifndef __EMPATHY_PRESENCE_MANAGER_H__
#define __EMPATHY_PRESENCE_MANAGER_H__

#include <glib.h>

#include <telepathy-glib/account.h>
#include <telepathy-glib/enums.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_PRESENCE_MANAGER         (empathy_presence_manager_get_type ())
#define EMPATHY_PRESENCE_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_PRESENCE_MANAGER, EmpathyPresenceManager))
#define EMPATHY_PRESENCE_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), EMPATHY_TYPE_PRESENCE_MANAGER, EmpathyPresenceManagerClass))
#define EMPATHY_IS_PRESENCE_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_PRESENCE_MANAGER))
#define EMPATHY_IS_PRESENCE_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_PRESENCE_MANAGER))
#define EMPATHY_PRESENCE_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_PRESENCE_MANAGER, EmpathyPresenceManagerClass))

typedef struct _EmpathyPresenceManager      EmpathyPresenceManager;
typedef struct _EmpathyPresenceManagerClass EmpathyPresenceManagerClass;
typedef struct _EmpathyPresenceManagerPrivate EmpathyPresenceManagerPrivate;

struct _EmpathyPresenceManager {
  GObject parent;
  EmpathyPresenceManagerPrivate *priv;
};

struct _EmpathyPresenceManagerClass
{
  GObjectClass parent_class;
};

GType empathy_presence_manager_get_type (void) G_GNUC_CONST;

EmpathyPresenceManager * empathy_presence_manager_dup_singleton (void);

TpConnectionPresenceType empathy_presence_manager_get_state (
    EmpathyPresenceManager *self);

void empathy_presence_manager_set_state (EmpathyPresenceManager *self,
    TpConnectionPresenceType state);

void empathy_presence_manager_set_status (EmpathyPresenceManager *self,
                 const gchar *status);

void empathy_presence_manager_set_presence (EmpathyPresenceManager *self,
                 TpConnectionPresenceType state,
                 const gchar *status);

gboolean empathy_presence_manager_get_auto_away (EmpathyPresenceManager *self);

void empathy_presence_manager_set_auto_away (EmpathyPresenceManager *self,
    gboolean     auto_away);

TpConnectionPresenceType empathy_presence_manager_get_requested_presence (
    EmpathyPresenceManager *self,
    gchar **status,
    gchar **status_message);

gboolean empathy_presence_manager_account_is_just_connected (
    EmpathyPresenceManager *self,
    TpAccount *account);

G_END_DECLS

#endif /* __EMPATHY_PRESENCE_MANAGER_H__ */
