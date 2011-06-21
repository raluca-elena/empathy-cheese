/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Copyright (C) 2010 Thomas Meire <blackskad@gmail.com>
 *
 * The code contained in this file is free software; you can redistribute
 * it and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either version
 * 2.1 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this code; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __EMPATHY_SEARCH_BAR_H__
#define __EMPATHY_SEARCH_BAR_H__

#include <glib.h>
#include <glib-object.h>

#include "empathy-chat-view.h"

G_BEGIN_DECLS

#define EMPATHY_TYPE_SEARCH_BAR (empathy_search_bar_get_type ())
#define EMPATHY_SEARCH_BAR(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), \
    EMPATHY_TYPE_SEARCH_BAR, EmpathySearchBar))
#define EMPATHY_SEARCH_BAR_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), \
    EMPATHY_TYPE_SEARCH_BAR, EmpathySearchBarClass))
#define EMPATHY_IS_SEARCH_BAR(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), \
    EMPATHY_TYPE_SEARCH_BAR))
#define EMPATHY_IS_SEARCH_BAR_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), \
    EMPATHY_TYPE_SEARCH_BAR))
#define EMPATHY_SEARCH_BAR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o),\
    EMPATHY_TYPE_SEARCH_BAR, EmpathySearchBarClass))

typedef struct _EmpathySearchBar EmpathySearchBar;
typedef struct _EmpathySearchBarClass EmpathySearchBarClass;

struct _EmpathySearchBar
{
  GtkBox parent;

  /*<private>*/
  gpointer priv;
};

struct _EmpathySearchBarClass
{
  GtkBoxClass parent_class;
};

GType       empathy_search_bar_get_type (void) G_GNUC_CONST;
GtkWidget * empathy_search_bar_new      (EmpathyChatView  *view);
void        empathy_search_bar_show     (EmpathySearchBar *searchbar);
void        empathy_search_bar_hide     (EmpathySearchBar *searchbar);
void        empathy_search_bar_paste_clipboard (EmpathySearchBar *searchbar);

G_END_DECLS

#endif /* __EMPATHY_SEARCH_BAR_H__ */
