/*
 * Copyright (C) 2010 Collabora Ltd.
 * Copyright (C) 2007-2010 Nokia Corporation.
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
 * Authors: Felix Kaser <felix.kaser@collabora.co.uk>
 *          Xavier Claessens <xavier.claessens@collabora.co.uk>
 *          Claudio Saavedra <csaavedra@igalia.com>
 */

#ifndef __EMPATHY_LIVE_SEARCH_H__
#define __EMPATHY_LIVE_SEARCH_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_LIVE_SEARCH         (empathy_live_search_get_type ())
#define EMPATHY_LIVE_SEARCH(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_LIVE_SEARCH, EmpathyLiveSearch))
#define EMPATHY_LIVE_SEARCH_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), EMPATHY_TYPE_LIVE_SEARCH, EmpathyLiveSearchClass))
#define EMPATHY_IS_LIVE_SEARCH(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_LIVE_SEARCH))
#define EMPATHY_IS_LIVE_SEARCH_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_LIVE_SEARCH))
#define EMPATHY_LIVE_SEARCH_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_LIVE_SEARCH, EmpathyLiveSearchClass))

typedef struct _EmpathyLiveSearch      EmpathyLiveSearch;
typedef struct _EmpathyLiveSearchClass EmpathyLiveSearchClass;

struct _EmpathyLiveSearch {
  GtkHBox parent;

  /*<private>*/
  gpointer priv;
};

struct _EmpathyLiveSearchClass {
  GtkHBoxClass parent_class;
};

GType empathy_live_search_get_type (void) G_GNUC_CONST;
GtkWidget *empathy_live_search_new (GtkWidget *hook);

GtkWidget *empathy_live_search_get_hook_widget (EmpathyLiveSearch *self);
void empathy_live_search_set_hook_widget (EmpathyLiveSearch *self,
    GtkWidget *hook);

const gchar *empathy_live_search_get_text (EmpathyLiveSearch *self);
void empathy_live_search_set_text (EmpathyLiveSearch *self,
    const gchar *text);

gboolean empathy_live_search_match (EmpathyLiveSearch *self,
    const gchar *string);

GPtrArray * empathy_live_search_strip_utf8_string (const gchar *string);

gboolean empathy_live_search_match_words (const gchar *string,
    GPtrArray *words);

GPtrArray * empathy_live_search_get_words (EmpathyLiveSearch *self);

/* Made public for unit tests */
gboolean empathy_live_search_match_string (const gchar *string,
   const gchar *prefix);

G_END_DECLS

#endif /* __EMPATHY_LIVE_SEARCH_H__ */
