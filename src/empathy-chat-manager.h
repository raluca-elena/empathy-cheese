/*
 * empathy-chat-manager.h - Header for EmpathyChatManager
 * Copyright (C) 2010 Collabora Ltd.
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

#ifndef __EMPATHY_CHAT_MANAGER_H__
#define __EMPATHY_CHAT_MANAGER_H__

#include <glib-object.h>

#include <libempathy-gtk/empathy-chat.h>

G_BEGIN_DECLS

typedef struct _EmpathyChatManager EmpathyChatManager;
typedef struct _EmpathyChatManagerClass EmpathyChatManagerClass;

struct _EmpathyChatManagerClass
{
  GObjectClass parent_class;
};

struct _EmpathyChatManager
{
  GObject parent;
};

GType empathy_chat_manager_get_type (void);

/* TYPE MACROS */
#define EMPATHY_TYPE_CHAT_MANAGER \
  (empathy_chat_manager_get_type ())
#define EMPATHY_CHAT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EMPATHY_TYPE_CHAT_MANAGER, \
    EmpathyChatManager))
#define EMPATHY_CHAT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EMPATHY_TYPE_CHAT_MANAGER, \
    EmpathyChatManagerClass))
#define EMPATHY_IS_CHAT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EMPATHY_TYPE_CHAT_MANAGER))
#define EMPATHY_IS_CHAT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EMPATHY_TYPE_CHAT_MANAGER))
#define EMPATHY_CHAT_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_CHAT_MANAGER, \
    EmpathyChatManagerClass))

EmpathyChatManager *empathy_chat_manager_dup_singleton (void);

void empathy_chat_manager_closed_chat (EmpathyChatManager *self,
    EmpathyChat *chat);
void empathy_chat_manager_undo_closed_chat (EmpathyChatManager *self);
guint empathy_chat_manager_get_num_closed_chats (EmpathyChatManager *self);

G_END_DECLS

#endif /* #ifndef __EMPATHY_CHAT_MANAGER_H__*/
