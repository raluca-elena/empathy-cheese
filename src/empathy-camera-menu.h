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
 */

#ifndef __EMPATHY_CAMERA_MENU_H__
#define __EMPATHY_CAMERA_MENU_H__

#include <glib-object.h>

#include "empathy-call-window.h"

G_BEGIN_DECLS

#define EMPATHY_TYPE_CAMERA_MENU         (empathy_camera_menu_get_type ())
#define EMPATHY_CAMERA_MENU(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_CAMERA_MENU, EmpathyCameraMenu))
#define EMPATHY_CAMERA_MENU_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_CAMERA_MENU, EmpathyCameraMenuClass))
#define EMPATHY_IS_CAMERA_MENU(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_CAMERA_MENU))
#define EMPATHY_IS_CAMERA_MENU_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_CAMERA_MENU))
#define EMPATHY_CAMERA_MENU_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_CAMERA_MENU, EmpathyCameraMenuClass))

typedef struct _EmpathyCameraMenu        EmpathyCameraMenu;
typedef struct _EmpathyCameraMenuPrivate EmpathyCameraMenuPrivate;
typedef struct _EmpathyCameraMenuClass   EmpathyCameraMenuClass;

struct _EmpathyCameraMenu
{
  GObject parent;
  EmpathyCameraMenuPrivate *priv;
};

struct _EmpathyCameraMenuClass
{
  GObjectClass parent_class;
};

GType empathy_camera_menu_get_type (void) G_GNUC_CONST;

EmpathyCameraMenu * empathy_camera_menu_new (EmpathyCallWindow *window);

G_END_DECLS

#endif /* __EMPATHY_CAMERA_MENU_H__ */
