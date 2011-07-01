/*
 * Copyright (C) 2006-2007 Imendio AB
 * Copyright (C) 2007-2011 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 *
 * Authors: Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 *          Emilio Pozuelo Monfort <emilio.pozuelo@collabora.co.uk>
 */

#ifndef __EMPATHY_LOG_WINDOW_H__
#define __EMPATHY_LOG_WINDOW_H__

#include <telepathy-glib/account.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_LOG_WINDOW	(empathy_log_window_get_type ())
#define EMPATHY_LOG_WINDOW(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), EMPATHY_TYPE_LOG_WINDOW, EmpathyLogWindow))
#define EMPATHY_LOG_WINDOW_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST ((obj), EMPATHY_TYPE_LOG_WINDOW, EmpathyLogWindowClass))
#define EMPATHY_IS_LOG_WINDOW(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), EMPATHY_TYPE_LOG_WINDOW))
#define EMPATHY_IS_LOG_WINDOW_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((obj), EMPATHY_TYPE_LOG_WINDOW))
#define EMPATHY_LOG_WINDOW_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_LOG_WINDOW, EmpathyLogWindowClass))

typedef struct _EmpathyLogWindow EmpathyLogWindow;
typedef struct _EmpathyLogWindowPriv EmpathyLogWindowPriv;
typedef struct _EmpathyLogWindowClass EmpathyLogWindowClass;

struct _EmpathyLogWindow
{
  GtkDialog parent;

  EmpathyLogWindowPriv *priv;
};

struct _EmpathyLogWindowClass
{
  GtkDialogClass parent_class;
};

GType empathy_log_window_get_type (void);

GtkWidget * empathy_log_window_show (TpAccount *account,
    const gchar *chat_id,
    gboolean chatroom,
    GtkWindow *parent);

G_END_DECLS

#endif /* __EMPATHY_LOG_WINDOW_H__ */
