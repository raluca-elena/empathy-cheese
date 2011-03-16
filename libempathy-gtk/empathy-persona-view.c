/*
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2007-2008, 2010 Collabora Ltd.
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
 * Authors: Mikael Hallendal <micke@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 *          Philip Withnall <philip.withnall@collabora.co.uk>
 *
 * Based off EmpathyContactListView.
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <telepathy-glib/util.h>

#include <folks/folks.h>
#include <folks/folks-telepathy.h>

#include <libempathy/empathy-individual-manager.h>
#include <libempathy/empathy-utils.h>

#include "empathy-persona-view.h"
#include "empathy-contact-widget.h"
#include "empathy-images.h"
#include "empathy-cell-renderer-text.h"
#include "empathy-cell-renderer-activatable.h"
#include "empathy-gtk-enum-types.h"
#include "empathy-gtk-marshal.h"
#include "empathy-ui-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_CONTACT
#include <libempathy/empathy-debug.h>

/**
 * SECTION:empathy-persona-view
 * @title: EmpathyPersonaView
 * @short_description: A tree view which displays personas from an individual
 * @include: libempathy-gtk/empathy-persona-view.h
 *
 * #EmpathyPersonaView is a tree view widget which displays the personas from
 * a given #EmpathyPersonaStore.
 *
 * It supports hiding offline personas and highlighting active personas. Active
 * personas are those which have recently changed state (e.g. online, offline or
 * from normal to a busy state).
 */

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyPersonaView)

typedef struct
{
  GtkTreeModelFilter *filter;
  GtkWidget *tooltip_widget;
  gboolean show_offline;
  EmpathyPersonaViewFeatureFlags features;
} EmpathyPersonaViewPriv;

enum
{
  PROP_0,
  PROP_MODEL,
  PROP_SHOW_OFFLINE,
  PROP_FEATURES,
};

enum DndDragType
{
  DND_DRAG_TYPE_INDIVIDUAL_ID,
  DND_DRAG_TYPE_PERSONA_ID,
  DND_DRAG_TYPE_STRING,
};

#define DRAG_TYPE(T,I) \
  { (gchar *) T, 0, I }

static const GtkTargetEntry drag_types_dest[] = {
  DRAG_TYPE ("text/individual-id", DND_DRAG_TYPE_INDIVIDUAL_ID),
  DRAG_TYPE ("text/plain", DND_DRAG_TYPE_STRING),
  DRAG_TYPE ("STRING", DND_DRAG_TYPE_STRING),
};

static const GtkTargetEntry drag_types_source[] = {
  DRAG_TYPE ("text/persona-id", DND_DRAG_TYPE_PERSONA_ID),
};

#undef DRAG_TYPE

static GdkAtom drag_atoms_dest[G_N_ELEMENTS (drag_types_dest)];
static GdkAtom drag_atoms_source[G_N_ELEMENTS (drag_types_source)];

enum
{
  DRAG_INDIVIDUAL_RECEIVED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (EmpathyPersonaView, empathy_persona_view, GTK_TYPE_TREE_VIEW);

static gboolean
filter_visible_func (GtkTreeModel *model,
    GtkTreeIter *iter,
    EmpathyPersonaView *self)
{
  EmpathyPersonaViewPriv *priv = GET_PRIV (self);
  gboolean is_online;

  gtk_tree_model_get (model, iter,
      EMPATHY_PERSONA_STORE_COL_IS_ONLINE, &is_online,
      -1);

  return (priv->show_offline || is_online);
}

static void
set_model (EmpathyPersonaView *self,
    GtkTreeModel *model)
{
  EmpathyPersonaViewPriv *priv = GET_PRIV (self);

  tp_clear_object (&priv->filter);

  priv->filter = GTK_TREE_MODEL_FILTER (gtk_tree_model_filter_new (model,
      NULL));
  gtk_tree_model_filter_set_visible_func (priv->filter,
      (GtkTreeModelFilterVisibleFunc) filter_visible_func, self, NULL);

  gtk_tree_view_set_model (GTK_TREE_VIEW (self), GTK_TREE_MODEL (priv->filter));
}

static void
tooltip_destroy_cb (GtkWidget *widget,
    EmpathyPersonaView *self)
{
  EmpathyPersonaViewPriv *priv = GET_PRIV (self);

  if (priv->tooltip_widget)
    {
      DEBUG ("Tooltip destroyed");
      g_object_unref (priv->tooltip_widget);
      priv->tooltip_widget = NULL;
    }
}

static gboolean
query_tooltip_cb (EmpathyPersonaView *self,
    gint x,
    gint y,
    gboolean keyboard_mode,
    GtkTooltip *tooltip,
    gpointer user_data)
{
  EmpathyPersonaViewPriv *priv = GET_PRIV (self);
  FolksPersona *persona;
  EmpathyContact *contact;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreePath *path;
  static gint running = 0;
  gboolean ret = FALSE;

  /* Avoid an infinite loop. See GNOME bug #574377 */
  if (running > 0)
    return FALSE;
  running++;

  if (!gtk_tree_view_get_tooltip_context (GTK_TREE_VIEW (self), &x, &y,
      keyboard_mode, &model, &path, &iter))
    {
      goto OUT;
    }

  gtk_tree_view_set_tooltip_row (GTK_TREE_VIEW (self), tooltip, path);
  gtk_tree_path_free (path);

  gtk_tree_model_get (model, &iter,
      EMPATHY_PERSONA_STORE_COL_PERSONA, &persona,
      -1);
  if (persona == NULL)
    goto OUT;

  contact = empathy_contact_dup_from_tp_contact (tpf_persona_get_contact (
      TPF_PERSONA (persona)));

  if (priv->tooltip_widget == NULL)
    {
      priv->tooltip_widget = empathy_contact_widget_new (contact,
          EMPATHY_CONTACT_WIDGET_FOR_TOOLTIP |
          EMPATHY_CONTACT_WIDGET_SHOW_LOCATION);
      gtk_container_set_border_width (GTK_CONTAINER (priv->tooltip_widget), 8);
      g_object_ref (priv->tooltip_widget);
      g_signal_connect (priv->tooltip_widget, "destroy",
          (GCallback) tooltip_destroy_cb, self);
      gtk_widget_show (priv->tooltip_widget);
    }
  else
    {
      empathy_contact_widget_set_contact (priv->tooltip_widget, contact);
    }

  gtk_tooltip_set_custom (tooltip, priv->tooltip_widget);
  ret = TRUE;

  g_object_unref (contact);
  g_object_unref (persona);

OUT:
  running--;

  return ret;
}

static void
cell_set_background (EmpathyPersonaView *self,
    GtkCellRenderer *cell,
    gboolean is_active)
{
  if (is_active)
    {
      GdkRGBA color;
      GtkStyleContext *style;

      style = gtk_widget_get_style_context (GTK_WIDGET (self));

      gtk_style_context_get_background_color (style, GTK_STATE_FLAG_SELECTED,
          &color);

      /* Here we take the current theme colour and add it to
       * the colour for white and average the two. This
       * gives a colour which is inline with the theme but
       * slightly whiter.
       */
      empathy_make_color_whiter (&color);

      g_object_set (cell, "cell-background-rgba", &color, NULL);
    }
  else
    {
      g_object_set (cell, "cell-background-rgba", NULL, NULL);
    }
}

static void
pixbuf_cell_data_func (GtkTreeViewColumn *tree_column,
    GtkCellRenderer *cell,
    GtkTreeModel *model,
    GtkTreeIter *iter,
    EmpathyPersonaView *self)
{
  GdkPixbuf *pixbuf;
  gboolean is_active;

  gtk_tree_model_get (model, iter,
      EMPATHY_PERSONA_STORE_COL_IS_ACTIVE, &is_active,
      EMPATHY_PERSONA_STORE_COL_ICON_STATUS, &pixbuf,
      -1);

  g_object_set (cell, "pixbuf", pixbuf, NULL);
  tp_clear_object (&pixbuf);

  cell_set_background (self, cell, is_active);
}

static void
audio_call_cell_data_func (GtkTreeViewColumn *tree_column,
    GtkCellRenderer *cell,
    GtkTreeModel *model,
    GtkTreeIter *iter,
    EmpathyPersonaView *self)
{
  gboolean is_active;
  gboolean can_audio, can_video;

  gtk_tree_model_get (model, iter,
      EMPATHY_PERSONA_STORE_COL_IS_ACTIVE, &is_active,
      EMPATHY_PERSONA_STORE_COL_CAN_AUDIO_CALL, &can_audio,
      EMPATHY_PERSONA_STORE_COL_CAN_VIDEO_CALL, &can_video,
      -1);

  g_object_set (cell,
      "visible", (can_audio || can_video),
      "icon-name", can_video? EMPATHY_IMAGE_VIDEO_CALL : EMPATHY_IMAGE_VOIP,
      NULL);

  cell_set_background (self, cell, is_active);
}

static void
avatar_cell_data_func (GtkTreeViewColumn *tree_column,
    GtkCellRenderer *cell,
    GtkTreeModel *model,
    GtkTreeIter *iter,
    EmpathyPersonaView *self)
{
  GdkPixbuf *pixbuf;
  gboolean show_avatar, is_active;

  gtk_tree_model_get (model, iter,
      EMPATHY_PERSONA_STORE_COL_PIXBUF_AVATAR, &pixbuf,
      EMPATHY_PERSONA_STORE_COL_PIXBUF_AVATAR_VISIBLE, &show_avatar,
      EMPATHY_PERSONA_STORE_COL_IS_ACTIVE, &is_active,
      -1);

  g_object_set (cell,
      "visible", show_avatar,
      "pixbuf", pixbuf,
      NULL);

  tp_clear_object (&pixbuf);

  cell_set_background (self, cell, is_active);
}

static void
text_cell_data_func (GtkTreeViewColumn *tree_column,
    GtkCellRenderer *cell,
    GtkTreeModel *model,
    GtkTreeIter *iter,
    EmpathyPersonaView *self)
{
  gboolean is_active;

  gtk_tree_model_get (model, iter,
      EMPATHY_PERSONA_STORE_COL_IS_ACTIVE, &is_active,
      -1);

  cell_set_background (self, cell, is_active);
}

static gboolean
individual_drag_received (EmpathyPersonaView *self,
    GdkDragContext *context,
    GtkSelectionData *selection)
{
  EmpathyIndividualManager *manager = NULL;
  FolksIndividual *individual;
  const gchar *individual_id;
  gboolean success = FALSE;

  individual_id = (const gchar *) gtk_selection_data_get_data (selection);
  manager = empathy_individual_manager_dup_singleton ();
  individual = empathy_individual_manager_lookup_member (manager,
      individual_id);

  if (individual == NULL)
    {
      DEBUG ("Failed to find drag event individual with ID '%s'",
          individual_id);
      g_object_unref (manager);
      return FALSE;
    }

  /* Emit a signal notifying of the drag. */
  g_signal_emit (self, signals[DRAG_INDIVIDUAL_RECEIVED], 0,
      gdk_drag_context_get_selected_action (context), individual, &success);

  g_object_unref (manager);

  return success;
}

static void
drag_data_received (GtkWidget *widget,
    GdkDragContext *context,
    gint x,
    gint y,
    GtkSelectionData *selection,
    guint info,
    guint time_)
{
  EmpathyPersonaView *self = EMPATHY_PERSONA_VIEW (widget);
  gboolean success = TRUE;

  if (info == DND_DRAG_TYPE_INDIVIDUAL_ID || info == DND_DRAG_TYPE_STRING)
    success = individual_drag_received (self, context, selection);

  gtk_drag_finish (context, success, FALSE, GDK_CURRENT_TIME);
}

static gboolean
drag_motion (GtkWidget *widget,
    GdkDragContext *context,
    gint x,
    gint y,
    guint time_)
{
  EmpathyPersonaView *self = EMPATHY_PERSONA_VIEW (widget);
  GdkAtom target;

  target = gtk_drag_dest_find_target (GTK_WIDGET (self), context, NULL);

  if (target == drag_atoms_dest[DND_DRAG_TYPE_INDIVIDUAL_ID])
    {
      GtkTreePath *path;

      /* FIXME: It doesn't make sense for us to highlight a specific row or
       * position to drop an Individual in, so just highlight the entire
       * widget.
       * Since I can't find a way to do this, just highlight the first possible
       * position in the tree. */
      gdk_drag_status (context, gdk_drag_context_get_suggested_action (context),
          time_);

      path = gtk_tree_path_new_first ();
      gtk_tree_view_set_drag_dest_row (GTK_TREE_VIEW (self), path,
          GTK_TREE_VIEW_DROP_BEFORE);
      gtk_tree_path_free (path);

      return TRUE;
    }

  /* Unknown or unhandled drag target */
  gdk_drag_status (context, GDK_ACTION_DEFAULT, time_);
  gtk_tree_view_set_drag_dest_row (GTK_TREE_VIEW (self), NULL, 0);

  return FALSE;
}

static void
drag_data_get (GtkWidget *widget,
    GdkDragContext *context,
    GtkSelectionData *selection,
    guint info,
    guint time_)
{
  EmpathyPersonaView *self = EMPATHY_PERSONA_VIEW (widget);
  FolksPersona *persona;
  const gchar *persona_uid;

  if (info != DND_DRAG_TYPE_PERSONA_ID)
    return;

  persona = empathy_persona_view_dup_selected (self);
  if (persona == NULL)
    return;

  persona_uid = folks_persona_get_uid (persona);
  gtk_selection_data_set (selection, drag_atoms_source[info], 8,
      (guchar *) persona_uid, strlen (persona_uid) + 1);

  g_object_unref (persona);
}

static gboolean
drag_drop (GtkWidget *widget,
    GdkDragContext *drag_context,
    gint x,
    gint y,
    guint time_)
{
  return FALSE;
}

static void
set_features (EmpathyPersonaView *self,
    EmpathyPersonaViewFeatureFlags features)
{
  EmpathyPersonaViewPriv *priv = GET_PRIV (self);

  priv->features = features;

  /* Setting reorderable is a hack that gets us row previews as drag icons
     for free.  We override all the drag handlers.  It's tricky to get the
     position of the drag icon right in drag_begin.  GtkTreeView has special
     voodoo for it, so we let it do the voodoo that he do (but only if dragging
     is enabled). */
  gtk_tree_view_set_reorderable (GTK_TREE_VIEW (self),
      (features & EMPATHY_PERSONA_VIEW_FEATURE_PERSONA_DRAG));

  /* Update DnD source/dest */
  if (features & EMPATHY_PERSONA_VIEW_FEATURE_PERSONA_DRAG)
    {
      gtk_drag_source_set (GTK_WIDGET (self),
          GDK_BUTTON1_MASK,
          drag_types_source,
          G_N_ELEMENTS (drag_types_source),
          GDK_ACTION_MOVE | GDK_ACTION_COPY);
    }
  else
    {
      gtk_drag_source_unset (GTK_WIDGET (self));
    }

  if (features & EMPATHY_PERSONA_VIEW_FEATURE_PERSONA_DROP)
    {
      gtk_drag_dest_set (GTK_WIDGET (self),
          GTK_DEST_DEFAULT_ALL,
          drag_types_dest,
          G_N_ELEMENTS (drag_types_dest), GDK_ACTION_MOVE | GDK_ACTION_COPY);
    }
  else
    {
      gtk_drag_dest_unset (GTK_WIDGET (self));
    }

  g_object_notify (G_OBJECT (self), "features");
}

static void
empathy_persona_view_init (EmpathyPersonaView *self)
{
  EmpathyPersonaViewPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
    EMPATHY_TYPE_PERSONA_VIEW, EmpathyPersonaViewPriv);

  self->priv = priv;

  /* Connect to tree view signals rather than override. */
  g_signal_connect (self, "query-tooltip", (GCallback) query_tooltip_cb, NULL);
}

static void
constructed (GObject *object)
{
  EmpathyPersonaView *self = EMPATHY_PERSONA_VIEW (object);
  GtkCellRenderer *cell;
  GtkTreeViewColumn *col;
  guint i;

  /* Set up view */
  g_object_set (self,
      "headers-visible", FALSE,
      "show-expanders", FALSE,
      NULL);

  col = gtk_tree_view_column_new ();

  /* State */
  cell = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (col, cell, FALSE);
  gtk_tree_view_column_set_cell_data_func (col, cell,
      (GtkTreeCellDataFunc) pixbuf_cell_data_func, self, NULL);

  g_object_set (cell,
      "xpad", 5,
      "ypad", 1,
      "visible", TRUE,
      NULL);

  /* Name */
  cell = empathy_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (col, cell, TRUE);
  gtk_tree_view_column_set_cell_data_func (col, cell,
      (GtkTreeCellDataFunc) text_cell_data_func, self, NULL);

  /* We (ab)use the name and status properties here to display display ID and
   * account name, respectively. Harmless. */
  gtk_tree_view_column_add_attribute (col, cell,
      "name", EMPATHY_PERSONA_STORE_COL_DISPLAY_ID);
  gtk_tree_view_column_add_attribute (col, cell,
      "text", EMPATHY_PERSONA_STORE_COL_DISPLAY_ID);
  gtk_tree_view_column_add_attribute (col, cell,
      "presence-type", EMPATHY_PERSONA_STORE_COL_PRESENCE_TYPE);
  gtk_tree_view_column_add_attribute (col, cell,
      "status", EMPATHY_PERSONA_STORE_COL_ACCOUNT_NAME);

  /* Audio Call Icon */
  cell = empathy_cell_renderer_activatable_new ();
  gtk_tree_view_column_pack_start (col, cell, FALSE);
  gtk_tree_view_column_set_cell_data_func (col, cell,
      (GtkTreeCellDataFunc) audio_call_cell_data_func, self, NULL);

  g_object_set (cell,
      "visible", FALSE,
      NULL);

  /* Avatar */
  cell = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (col, cell, FALSE);
  gtk_tree_view_column_set_cell_data_func (col, cell,
      (GtkTreeCellDataFunc) avatar_cell_data_func, self, NULL);

  g_object_set (cell,
      "xpad", 0,
      "ypad", 0,
      "visible", FALSE,
      "width", 32,
      "height", 32,
      NULL);

  /* Actually add the column now we have added all cell renderers */
  gtk_tree_view_append_column (GTK_TREE_VIEW (self), col);

  /* Drag & Drop. */
  for (i = 0; i < G_N_ELEMENTS (drag_types_dest); ++i)
    drag_atoms_dest[i] = gdk_atom_intern (drag_types_dest[i].target, FALSE);

  for (i = 0; i < G_N_ELEMENTS (drag_types_source); ++i)
    drag_atoms_source[i] = gdk_atom_intern (drag_types_source[i].target, FALSE);
}

static void
get_property (GObject *object,
    guint param_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyPersonaViewPriv *priv = GET_PRIV (object);

  switch (param_id)
    {
      case PROP_MODEL:
        g_value_set_object (value, priv->filter);
        break;
      case PROP_SHOW_OFFLINE:
        g_value_set_boolean (value, priv->show_offline);
        break;
      case PROP_FEATURES:
        g_value_set_flags (value, priv->features);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    }
}

static void
set_property (GObject *object,
    guint param_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyPersonaView *self = EMPATHY_PERSONA_VIEW (object);

  switch (param_id)
    {
      case PROP_MODEL:
        set_model (self, g_value_get_object (value));
        break;
      case PROP_SHOW_OFFLINE:
        empathy_persona_view_set_show_offline (self,
            g_value_get_boolean (value));
        break;
      case PROP_FEATURES:
        set_features (self, g_value_get_flags (value));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    }
}

static void
dispose (GObject *object)
{
  EmpathyPersonaView *self = EMPATHY_PERSONA_VIEW (object);
  EmpathyPersonaViewPriv *priv = GET_PRIV (self);

  tp_clear_object (&priv->filter);

  if (priv->tooltip_widget)
    gtk_widget_destroy (priv->tooltip_widget);
  priv->tooltip_widget = NULL;

  G_OBJECT_CLASS (empathy_persona_view_parent_class)->dispose (object);
}

static void
empathy_persona_view_class_init (EmpathyPersonaViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = constructed;
  object_class->dispose = dispose;
  object_class->get_property = get_property;
  object_class->set_property = set_property;

  widget_class->drag_data_received = drag_data_received;
  widget_class->drag_drop = drag_drop;
  widget_class->drag_data_get = drag_data_get;
  widget_class->drag_motion = drag_motion;

  signals[DRAG_INDIVIDUAL_RECEIVED] =
      g_signal_new ("drag-individual-received",
      G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (EmpathyPersonaViewClass, drag_individual_received),
      NULL, NULL,
      _empathy_gtk_marshal_BOOLEAN__UINT_OBJECT,
      G_TYPE_BOOLEAN, 2, G_TYPE_UINT, FOLKS_TYPE_INDIVIDUAL);

  /* We override the "model" property so that we can wrap it in a
   * GtkTreeModelFilter for showing/hiding offline personas. */
  g_object_class_override_property (object_class, PROP_MODEL, "model");

  /**
   * EmpathyPersonaStore:show-offline:
   *
   * Whether to display offline personas.
   */
  g_object_class_install_property (object_class, PROP_SHOW_OFFLINE,
      g_param_spec_boolean ("show-offline",
          "Show Offline",
          "Whether to display offline personas.",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * EmpathyPersonaStore:features:
   *
   * Features of the view, such as whether drag and drop is enabled.
   */
  g_object_class_install_property (object_class, PROP_FEATURES,
      g_param_spec_flags ("features",
          "Features",
          "Flags for all enabled features.",
          EMPATHY_TYPE_PERSONA_VIEW_FEATURE_FLAGS,
          EMPATHY_PERSONA_VIEW_FEATURE_NONE,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (object_class, sizeof (EmpathyPersonaViewPriv));
}

/**
 * empathy_persona_view_new:
 * @store: an #EmpathyPersonaStore
 * @features: a set of flags specifying the view's functionality, or
 * %EMPATHY_PERSONA_VIEW_FEATURE_NONE
 *
 * Create a new #EmpathyPersonaView displaying the personas in
 * #EmpathyPersonaStore.
 *
 * Return value: a new #EmpathyPersonaView
 */
EmpathyPersonaView *
empathy_persona_view_new (EmpathyPersonaStore *store,
    EmpathyPersonaViewFeatureFlags features)
{
  g_return_val_if_fail (EMPATHY_IS_PERSONA_STORE (store), NULL);

  return g_object_new (EMPATHY_TYPE_PERSONA_VIEW,
      "model", store,
      "features", features,
      NULL);
}

/**
 * empathy_persona_view_dup_selected:
 * @self: an #EmpathyPersonaView
 *
 * Return the #FolksPersona associated with the currently selected row. The
 * persona is referenced before being returned. If no row is selected, %NULL is
 * returned.
 *
 * Return value: the currently selected #FolksPersona, or %NULL; unref with
 * g_object_unref()
 */
FolksPersona *
empathy_persona_view_dup_selected (EmpathyPersonaView *self)
{
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  GtkTreeModel *model;
  FolksPersona *persona;

  g_return_val_if_fail (EMPATHY_IS_PERSONA_VIEW (self), NULL);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (self));
  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    return NULL;

  gtk_tree_model_get (model, &iter,
      EMPATHY_PERSONA_STORE_COL_PERSONA, &persona,
      -1);

  return persona;
}

/**
 * empathy_persona_view_get_show_offline:
 * @self: an #EmpathyPersonaView
 *
 * Get the value of the #EmpathyPersonaView:show-offline property.
 *
 * Return value: %TRUE if offline personas are being shown, %FALSE otherwise
 */
gboolean
empathy_persona_view_get_show_offline (EmpathyPersonaView *self)
{
  g_return_val_if_fail (EMPATHY_IS_PERSONA_VIEW (self), FALSE);

  return GET_PRIV (self)->show_offline;
}

/**
 * empathy_persona_view_set_show_offline:
 * @self: an #EmpathyPersonaView
 * @show_offline: %TRUE to show personas which are offline, %FALSE otherwise
 *
 * Set the #EmpathyPersonaView:show-offline property to @show_offline.
 */
void
empathy_persona_view_set_show_offline (EmpathyPersonaView *self,
    gboolean show_offline)
{
  EmpathyPersonaViewPriv *priv;

  g_return_if_fail (EMPATHY_IS_PERSONA_VIEW (self));

  priv = GET_PRIV (self);
  priv->show_offline = show_offline;

  gtk_tree_model_filter_refilter (priv->filter);

  g_object_notify (G_OBJECT (self), "show-offline");
}
