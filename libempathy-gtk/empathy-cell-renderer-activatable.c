/*
 * Copyright (C) 2007 Raphael Slinckx <raphael@slinckx.net>
 * Copyright (C) 2007-2009 Collabora Ltd.
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
 * Authors: Raphael Slinckx <raphael@slinckx.net>
 *          Cosimo Cecchi   <cosimo.cecchi@collabora.co.uk>
 */

#include <config.h>

#include <gtk/gtk.h>

#include <libempathy/empathy-utils.h>

#include "empathy-cell-renderer-activatable.h"

enum {
  PATH_ACTIVATED,
  LAST_SIGNAL
};

enum {
  PROP_SHOW_ON_SELECT = 1
};

typedef struct {
  gboolean show_on_select;
} EmpathyCellRendererActivatablePriv;

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (EmpathyCellRendererActivatable,
    empathy_cell_renderer_activatable, GTK_TYPE_CELL_RENDERER_PIXBUF)

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyCellRendererActivatable)

static void
empathy_cell_renderer_activatable_init (EmpathyCellRendererActivatable *cell)
{
  cell->priv = G_TYPE_INSTANCE_GET_PRIVATE (cell,
      EMPATHY_TYPE_CELL_RENDERER_ACTIVATABLE,
      EmpathyCellRendererActivatablePriv);

  g_object_set (cell,
      "xpad", 0,
      "ypad", 0,
      "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
      "follow-state", TRUE,
      NULL);
}

static void
cell_renderer_activatable_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyCellRendererActivatablePriv *priv = GET_PRIV (object);

  switch (prop_id)
    {
      case PROP_SHOW_ON_SELECT:
        g_value_set_boolean (value, priv->show_on_select);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
cell_renderer_activatable_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyCellRendererActivatablePriv *priv = GET_PRIV (object);

  switch (prop_id)
    {
      case PROP_SHOW_ON_SELECT:
        priv->show_on_select = g_value_get_boolean (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

GtkCellRenderer *
empathy_cell_renderer_activatable_new (void)
{
  return g_object_new (EMPATHY_TYPE_CELL_RENDERER_ACTIVATABLE, NULL);
}

static gboolean
cell_renderer_activatable_activate (GtkCellRenderer      *cell,
    GdkEvent *event,
    GtkWidget *widget,
    const gchar *path_string,
    const GdkRectangle *background_area,
    const GdkRectangle *cell_area,
    GtkCellRendererState  flags)
{
  EmpathyCellRendererActivatable *activatable;
  gint ex, ey, bx, by, bw, bh;

  activatable = EMPATHY_CELL_RENDERER_ACTIVATABLE (cell);

  if (!GTK_IS_TREE_VIEW (widget) || event == NULL ||
      event->type != GDK_BUTTON_PRESS) {
    return FALSE;
  }

  ex  = (gint) ((GdkEventButton *) event)->x;
  ey  = (gint) ((GdkEventButton *) event)->y;
  bx = background_area->x;
  by = background_area->y;
  bw = background_area->width;
  bh = background_area->height;

  if (ex < bx || ex > (bx+bw) || ey < by || ey > (by+bh)){
    /* Click wasn't on the icon */
    return FALSE;
  }

  g_signal_emit (activatable, signals[PATH_ACTIVATED], 0, path_string);

  return TRUE;
}

static void
cell_renderer_activatable_render (
    GtkCellRenderer *cell,
    cairo_t *cr,
    GtkWidget *widget,
    const GdkRectangle *background_area,
    const GdkRectangle *cell_area,
    GtkCellRendererState flags)
{
  EmpathyCellRendererActivatablePriv *priv = GET_PRIV (cell);

  if (priv->show_on_select && !(flags & (GTK_CELL_RENDERER_SELECTED)))
    return;

  GTK_CELL_RENDERER_CLASS
    (empathy_cell_renderer_activatable_parent_class)->render (
        cell, cr, widget, background_area, cell_area, flags);
}

static void
empathy_cell_renderer_activatable_class_init (
    EmpathyCellRendererActivatableClass *klass)
{
  GtkCellRendererClass *cell_class;
  GObjectClass *oclass;

  oclass = G_OBJECT_CLASS (klass);
  oclass->get_property = cell_renderer_activatable_get_property;
  oclass->set_property = cell_renderer_activatable_set_property;

  cell_class = GTK_CELL_RENDERER_CLASS (klass);
  cell_class->activate = cell_renderer_activatable_activate;
  cell_class->render = cell_renderer_activatable_render;

  signals[PATH_ACTIVATED] =
    g_signal_new ("path-activated",
        G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        g_cclosure_marshal_generic,
        G_TYPE_NONE,
        1, G_TYPE_STRING);

  g_object_class_install_property (oclass, PROP_SHOW_ON_SELECT,
      g_param_spec_boolean ("show-on-select",
          "Show on select",
          "Whether the cell renderer should be shown only when it's selected",
          FALSE,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  g_type_class_add_private (klass,
      sizeof (EmpathyCellRendererActivatablePriv));
}
