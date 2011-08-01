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

#ifndef __EMPATHY_MIC_MENU_H__
#define __EMPATHY_MIC_MENU_H__

#include <glib-object.h>

#include "empathy-call-window.h"

G_BEGIN_DECLS

#define EMPATHY_TYPE_MIC_MENU         (empathy_mic_menu_get_type ())
#define EMPATHY_MIC_MENU(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_MIC_MENU, EmpathyMicMenu))
#define EMPATHY_MIC_MENU_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_MIC_MENU, EmpathyMicMenuClass))
#define EMPATHY_IS_MIC_MENU(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_MIC_MENU))
#define EMPATHY_IS_MIC_MENU_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_MIC_MENU))
#define EMPATHY_MIC_MENU_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_MIC_MENU, EmpathyMicMenuClass))

typedef struct _EmpathyMicMenu        EmpathyMicMenu;
typedef struct _EmpathyMicMenuPrivate EmpathyMicMenuPrivate;
typedef struct _EmpathyMicMenuClass   EmpathyMicMenuClass;

struct _EmpathyMicMenu
{
  GObject parent;
  EmpathyMicMenuPrivate *priv;
};

struct _EmpathyMicMenuClass
{
  GObjectClass parent_class;
};

GType empathy_mic_menu_get_type (void) G_GNUC_CONST;

EmpathyMicMenu * empathy_mic_menu_new (EmpathyCallWindow *window);

G_END_DECLS

#endif /* __EMPATHY_MIC_MENU_H__ */
