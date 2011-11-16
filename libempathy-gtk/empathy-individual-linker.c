/*
 * Copyright (C) 2010 Collabora Ltd.
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
 * Authors: Philip Withnall <philip.withnall@collabora.co.uk>
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <telepathy-glib/util.h>

#include <folks/folks.h>

#include <libempathy/empathy-individual-manager.h>
#include <libempathy/empathy-utils.h>

#include "empathy-individual-linker.h"
#include "empathy-individual-store.h"
#include "empathy-individual-view.h"
#include "empathy-individual-widget.h"
#include "empathy-persona-store.h"
#include "empathy-persona-view.h"

/**
 * SECTION:empathy-individual-linker
 * @title:EmpathyIndividualLinker
 * @short_description: A widget used to link together #FolksIndividual<!-- -->s
 * @include: libempathy-gtk/empathy-individual-linker.h
 *
 * #EmpathyIndividualLinker is a widget which allows selection of several
 * #FolksIndividual<!-- -->s to link together to form a single new individual.
 * The widget provides a preview of the linked individual.
 */

/**
 * EmpathyIndividualLinker:
 * @parent: parent object
 *
 * Widget which extends #GtkBin to provide a list of #FolksIndividual<!-- -->s
 * to link together.
 */

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyIndividualLinker)

typedef struct {
  EmpathyIndividualStore *individual_store; /* owned */
  EmpathyIndividualView *individual_view; /* child widget */
  GtkWidget *preview_widget; /* child widget */
  EmpathyPersonaStore *persona_store; /* owned */
  GtkTreeViewColumn *toggle_column; /* child widget */
  GtkCellRenderer *toggle_renderer; /* child widget */
  GtkWidget *search_widget; /* child widget */

  FolksIndividual *start_individual; /* owned, allow-none */
  FolksIndividual *new_individual; /* owned, allow-none */

  /* Stores the Individuals whose Personas have been added to the
   * new_individual */
  /* unowned Individual (borrowed from EmpathyIndividualStore) -> bool */
  GHashTable *changed_individuals;
} EmpathyIndividualLinkerPriv;

enum {
  PROP_START_INDIVIDUAL = 1,
  PROP_HAS_CHANGED,
};

G_DEFINE_TYPE (EmpathyIndividualLinker, empathy_individual_linker,
    GTK_TYPE_BOX);

static void
contact_toggle_cell_data_func (GtkTreeViewColumn *tree_column,
    GtkCellRenderer *cell,
    GtkTreeModel *tree_model,
    GtkTreeIter *iter,
    EmpathyIndividualLinker *self)
{
  EmpathyIndividualLinkerPriv *priv;
  FolksIndividual *individual;
  gboolean is_group, individual_added;

  priv = GET_PRIV (self);

  gtk_tree_model_get (tree_model, iter,
      EMPATHY_INDIVIDUAL_STORE_COL_IS_GROUP, &is_group,
      EMPATHY_INDIVIDUAL_STORE_COL_INDIVIDUAL, &individual,
      -1);

  individual_added = GPOINTER_TO_UINT (g_hash_table_lookup (
      priv->changed_individuals, individual));

  /* We don't want to show checkboxes next to the group rows.
   * All checkboxes should be sensitive except the checkbox for the start
   * individual, which should be permanently active and insensitive */
  g_object_set (cell,
      "visible", !is_group,
      "sensitive", individual != priv->start_individual,
      "activatable", individual != priv->start_individual,
      "active", individual_added || individual == priv->start_individual,
      NULL);

  tp_clear_object (&individual);
}

static void
update_toggle_renderers (EmpathyIndividualLinker *self)
{
  EmpathyIndividualLinkerPriv *priv = GET_PRIV (self);

  /* Re-setting the cell data func to the same function causes a refresh of the
   * entire column, ensuring that each toggle button is correctly active or
   * inactive. This is necessary because one Individual might appear multiple
   * times in the list (in different groups), so toggling one instance of the
   * Individual should toggle all of them. */
  gtk_tree_view_column_set_cell_data_func (priv->toggle_column,
      priv->toggle_renderer,
      (GtkTreeCellDataFunc) contact_toggle_cell_data_func, self, NULL);
}

static void
link_individual (EmpathyIndividualLinker *self,
    FolksIndividual *individual)
{
  EmpathyIndividualLinkerPriv *priv = GET_PRIV (self);
  GeeSet *old_personas, *new_personas;
  GeeHashSet *final_personas;
  gboolean personas_changed;

  /* Add the individual to the link */
  g_hash_table_insert (priv->changed_individuals, individual,
      GUINT_TO_POINTER (TRUE));

  /* Add personas which are in @individual to priv->new_individual, adding them
   * to the set of personas. */
  old_personas = folks_individual_get_personas (individual);
  new_personas = folks_individual_get_personas (priv->new_individual);
  final_personas = gee_hash_set_new (FOLKS_TYPE_PERSONA, g_object_ref,
      g_object_unref, g_direct_hash, g_direct_equal);
  gee_collection_add_all (GEE_COLLECTION (final_personas),
      GEE_COLLECTION (old_personas));
  personas_changed = gee_collection_add_all (GEE_COLLECTION (final_personas),
      GEE_COLLECTION (new_personas));

  /* avoid updating all values in the Individual if the set of personas doesn't
   * actually change */
  if (personas_changed)
    {
      folks_individual_set_personas (priv->new_individual,
          GEE_SET (final_personas));
    }

  g_clear_object (&final_personas);

  /* Update the toggle renderers, so that if this Individual is listed in
   * another group in the EmpathyIndividualView, the toggle button for that
   * group is updated. */
  update_toggle_renderers (self);

  g_object_notify (G_OBJECT (self), "has-changed");
}

static void
unlink_individual (EmpathyIndividualLinker *self,
    FolksIndividual *individual)
{
  EmpathyIndividualLinkerPriv *priv = GET_PRIV (self);
  GeeSet *removed_personas, *old_personas;
  GeeHashSet *final_personas;
  gboolean personas_changed;

  /* Remove the individual from the link */
  g_hash_table_remove (priv->changed_individuals, individual);

  /* Remove personas which are in @individual from priv->new_individual. */
  old_personas = folks_individual_get_personas (priv->new_individual);
  removed_personas = folks_individual_get_personas (individual);

  final_personas = gee_hash_set_new (FOLKS_TYPE_PERSONA, g_object_ref,
      g_object_unref, g_direct_hash, g_direct_equal);
  gee_collection_add_all (GEE_COLLECTION (final_personas),
      GEE_COLLECTION (old_personas));
  personas_changed = gee_collection_remove_all (GEE_COLLECTION (final_personas),
      GEE_COLLECTION (removed_personas));

  if (personas_changed)
    {
      folks_individual_set_personas (priv->new_individual,
          GEE_SET (final_personas));
    }

  g_clear_object (&final_personas);

  /* Update the toggle renderers, so that if this Individual is listed in
   * another group in the EmpathyIndividualView, the toggle button for that
   * group is updated. */
  update_toggle_renderers (self);

  g_object_notify (G_OBJECT (self), "has-changed");
}

static void
toggle_individual_row (EmpathyIndividualLinker *self,
    GtkTreePath *path)
{
  EmpathyIndividualLinkerPriv *priv = GET_PRIV (self);
  FolksIndividual *individual;
  GtkTreeIter iter;
  GtkTreeModel *tree_model;
  gboolean individual_added;

  tree_model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->individual_view));

  gtk_tree_model_get_iter (tree_model, &iter, path);
  gtk_tree_model_get (tree_model, &iter,
      EMPATHY_INDIVIDUAL_STORE_COL_INDIVIDUAL, &individual,
      -1);

  if (individual == NULL)
    return;

  individual_added = GPOINTER_TO_UINT (g_hash_table_lookup (
      priv->changed_individuals, individual));

  /* Toggle the Individual's linked status */
  if (individual_added)
    unlink_individual (self, individual);
  else
    link_individual (self, individual);

  g_object_unref (individual);
}

static void
row_activated_cb (EmpathyIndividualView *view,
    GtkTreePath *path,
    GtkTreeViewColumn *column,
    EmpathyIndividualLinker *self)
{
  toggle_individual_row (self, path);
}

static void
row_toggled_cb (GtkCellRendererToggle *cell_renderer,
    const gchar *path,
    EmpathyIndividualLinker *self)
{
  GtkTreePath *tree_path = gtk_tree_path_new_from_string (path);
  toggle_individual_row (self, tree_path);
  gtk_tree_path_free (tree_path);
}

static gboolean
individual_view_drag_motion_cb (GtkWidget *widget,
    GdkDragContext *context,
    gint x,
    gint y,
    guint time_)
{
  EmpathyIndividualView *view = EMPATHY_INDIVIDUAL_VIEW (widget);
  GdkAtom target;

  target = gtk_drag_dest_find_target (GTK_WIDGET (view), context, NULL);

  if (target == gdk_atom_intern_static_string ("text/x-persona-id"))
    {
      GtkTreePath *path;

      /* FIXME: It doesn't make sense for us to highlight a specific row or
       * position to drop a Persona in, so just highlight the entire widget.
       * Since I can't find a way to do this, just highlight the first possible
       * position in the tree. */
      gdk_drag_status (context, gdk_drag_context_get_suggested_action (context),
          time_);

      path = gtk_tree_path_new_first ();
      gtk_tree_view_set_drag_dest_row (GTK_TREE_VIEW (view), path,
          GTK_TREE_VIEW_DROP_BEFORE);
      gtk_tree_path_free (path);

      return TRUE;
    }

  /* Unknown or unhandled drag target */
  gdk_drag_status (context, GDK_ACTION_DEFAULT, time_);
  gtk_tree_view_set_drag_dest_row (GTK_TREE_VIEW (view), NULL, 0);

  return FALSE;
}

static gboolean
individual_view_drag_persona_received_cb (EmpathyIndividualView *view,
    GdkDragAction action,
    FolksPersona *persona,
    FolksIndividual *individual,
    EmpathyIndividualLinker *self)
{
  EmpathyIndividualLinkerPriv *priv = GET_PRIV (self);

  /* A Persona has been dragged onto the EmpathyIndividualView (from the
   * EmpathyPersonaView), so we try to remove the Individual which contains
   * the Persona from the link. */
  if (individual != priv->start_individual)
    {
      unlink_individual (self, individual);
      return TRUE;
    }

  return FALSE;
}

static gboolean
persona_view_drag_individual_received_cb (EmpathyPersonaView *view,
    GdkDragAction action,
    FolksIndividual *individual,
    EmpathyIndividualLinker *self)
{
  /* An Individual has been dragged onto the EmpathyPersonaView (from the
   * EmpathyIndividualView), so we try to add the Individual to the link. */
  link_individual (self, individual);

  return TRUE;
}

static void
set_up (EmpathyIndividualLinker *self)
{
  EmpathyIndividualLinkerPriv *priv;
  EmpathyIndividualManager *individual_manager;
  GtkWidget *top_vbox;
  GtkPaned *paned;
  GtkWidget *label, *scrolled_window;
  GtkBox *vbox;
  EmpathyPersonaView *persona_view;
  gchar *tmp;
  GtkWidget *alignment;

  priv = GET_PRIV (self);

  top_vbox = gtk_vbox_new (FALSE, 6);

  /* Layout panes */
  paned = GTK_PANED (gtk_hpaned_new ());

  /* Left column heading */
  alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
  gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 0, 0, 0, 6);
  gtk_widget_show (alignment);

  vbox = GTK_BOX (gtk_vbox_new (FALSE, 6));
  label = gtk_label_new (NULL);
  tmp = g_strdup_printf ("<b>%s</b>", _("Select contacts to link"));
  gtk_label_set_markup (GTK_LABEL (label), tmp);
  g_free (tmp);
  gtk_box_pack_start (vbox, label, FALSE, TRUE, 0);
  gtk_widget_show (label);

  /* Individual selector */
  individual_manager = empathy_individual_manager_dup_singleton ();
  priv->individual_store = empathy_individual_store_new (individual_manager);
  g_object_unref (individual_manager);

  empathy_individual_store_set_show_protocols (priv->individual_store, FALSE);

  priv->individual_view = empathy_individual_view_new (priv->individual_store,
      EMPATHY_INDIVIDUAL_VIEW_FEATURE_INDIVIDUAL_DRAG |
      EMPATHY_INDIVIDUAL_VIEW_FEATURE_INDIVIDUAL_DROP |
      EMPATHY_INDIVIDUAL_VIEW_FEATURE_PERSONA_DROP,
      EMPATHY_INDIVIDUAL_FEATURE_NONE);
  empathy_individual_view_set_show_offline (priv->individual_view, TRUE);
  empathy_individual_view_set_show_untrusted (priv->individual_view, FALSE);

  g_signal_connect (priv->individual_view, "row-activated",
      (GCallback) row_activated_cb, self);
  g_signal_connect (priv->individual_view, "drag-motion",
      (GCallback) individual_view_drag_motion_cb, self);
  g_signal_connect (priv->individual_view, "drag-persona-received",
      (GCallback) individual_view_drag_persona_received_cb, self);

  /* Add a checkbox column to the selector */
  priv->toggle_renderer = gtk_cell_renderer_toggle_new ();
  g_signal_connect (priv->toggle_renderer, "toggled",
      (GCallback) row_toggled_cb, self);

  priv->toggle_column = gtk_tree_view_column_new ();
  gtk_tree_view_column_pack_start (priv->toggle_column, priv->toggle_renderer,
      FALSE);
  gtk_tree_view_column_set_cell_data_func (priv->toggle_column,
      priv->toggle_renderer,
      (GtkTreeCellDataFunc) contact_toggle_cell_data_func, self, NULL);

  gtk_tree_view_insert_column (GTK_TREE_VIEW (priv->individual_view),
      priv->toggle_column, 0);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
      GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
      GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (scrolled_window),
      GTK_WIDGET (priv->individual_view));
  gtk_widget_show (GTK_WIDGET (priv->individual_view));

  gtk_box_pack_start (vbox, scrolled_window, TRUE, TRUE, 0);
  gtk_widget_show (scrolled_window);

  /* Live search */
  priv->search_widget = empathy_live_search_new (
      GTK_WIDGET (priv->individual_view));
  empathy_individual_view_set_live_search (priv->individual_view,
      EMPATHY_LIVE_SEARCH (priv->search_widget));

  gtk_box_pack_end (vbox, priv->search_widget, FALSE, TRUE, 0);

  gtk_container_add (GTK_CONTAINER (alignment), GTK_WIDGET (vbox));
  gtk_paned_pack1 (paned, alignment, TRUE, FALSE);
  gtk_widget_show (GTK_WIDGET (vbox));

  /* Right column heading */
  alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
  gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 0, 0, 6, 0);
  gtk_widget_show (alignment);

  vbox = GTK_BOX (gtk_vbox_new (FALSE, 6));
  label = gtk_label_new (NULL);
  tmp = g_strdup_printf ("<b>%s</b>", _("New contact preview"));
  gtk_label_set_markup (GTK_LABEL (label), tmp);
  g_free (tmp);
  gtk_box_pack_start (vbox, label, FALSE, TRUE, 0);
  gtk_widget_show (label);

  /* New individual preview */
  priv->preview_widget = empathy_individual_widget_new (priv->new_individual,
      EMPATHY_INDIVIDUAL_WIDGET_SHOW_DETAILS);
  gtk_box_pack_start (vbox, priv->preview_widget, FALSE, TRUE, 0);
  gtk_widget_show (priv->preview_widget);

  /* Persona list */
  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
      GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
      GTK_SHADOW_IN);

  priv->persona_store = empathy_persona_store_new (priv->new_individual);
  empathy_persona_store_set_show_protocols (priv->persona_store, TRUE);
  persona_view = empathy_persona_view_new (priv->persona_store,
      EMPATHY_PERSONA_VIEW_FEATURE_ALL);
  empathy_persona_view_set_show_offline (persona_view, TRUE);

  g_signal_connect (persona_view, "drag-individual-received",
      (GCallback) persona_view_drag_individual_received_cb, self);

  gtk_container_add (GTK_CONTAINER (scrolled_window),
      GTK_WIDGET (persona_view));
  gtk_widget_show (GTK_WIDGET (persona_view));

  gtk_box_pack_start (vbox, scrolled_window, TRUE, TRUE, 0);
  gtk_widget_show (scrolled_window);

  gtk_container_add (GTK_CONTAINER (alignment), GTK_WIDGET (vbox));
  gtk_paned_pack2 (paned, alignment, TRUE, FALSE);
  gtk_widget_show (GTK_WIDGET (vbox));

  gtk_widget_show (GTK_WIDGET (paned));

  /* Footer label */
  label = gtk_label_new (NULL);
  tmp = g_strdup_printf ("<i>%s</i>",
      _("Contacts selected in the list on the left will be linked together."));
  gtk_label_set_markup (GTK_LABEL (label), tmp);
  g_free (tmp);
  gtk_widget_show (label);

  gtk_box_pack_start (GTK_BOX (top_vbox), GTK_WIDGET (paned), TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (top_vbox), label, FALSE, TRUE, 0);

  /* Add the main vbox to the bin */
  gtk_box_pack_start (GTK_BOX (self), GTK_WIDGET (top_vbox), TRUE, TRUE, 0);
  gtk_widget_show (GTK_WIDGET (top_vbox));
}

static void
empathy_individual_linker_init (EmpathyIndividualLinker *self)
{
  EmpathyIndividualLinkerPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_INDIVIDUAL_LINKER, EmpathyIndividualLinkerPriv);

  self->priv = priv;

  priv->changed_individuals = g_hash_table_new (NULL, NULL);

  set_up (self);
}

static void
get_property (GObject *object,
    guint param_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyIndividualLinkerPriv *priv;

  priv = GET_PRIV (object);

  switch (param_id)
    {
      case PROP_START_INDIVIDUAL:
        g_value_set_object (value, priv->start_individual);
        break;
      case PROP_HAS_CHANGED:
        g_value_set_boolean (value, empathy_individual_linker_get_has_changed (
            EMPATHY_INDIVIDUAL_LINKER (object)));
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
  switch (param_id)
    {
      case PROP_START_INDIVIDUAL:
        empathy_individual_linker_set_start_individual (
            EMPATHY_INDIVIDUAL_LINKER (object), g_value_get_object (value));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    }
}

static void
dispose (GObject *object)
{
  EmpathyIndividualLinkerPriv *priv = GET_PRIV (object);

  tp_clear_object (&priv->individual_store);
  tp_clear_object (&priv->persona_store);
  tp_clear_object (&priv->start_individual);
  tp_clear_object (&priv->new_individual);

  G_OBJECT_CLASS (empathy_individual_linker_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
  EmpathyIndividualLinkerPriv *priv = GET_PRIV (object);

  g_hash_table_unref (priv->changed_individuals);

  G_OBJECT_CLASS (empathy_individual_linker_parent_class)->finalize (object);
}

static void
empathy_individual_linker_class_init (EmpathyIndividualLinkerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = get_property;
  object_class->set_property = set_property;
  object_class->dispose = dispose;
  object_class->finalize = finalize;

  /**
   * EmpathyIndividualLinker:start-individual:
   *
   * The #FolksIndividual to link other individuals to. This individual is
   * selected by default in the list of individuals, and cannot be unselected.
   * This ensures that empathy_individual_linker_get_linked_personas() will
   * always return at least one persona to link.
   */
  g_object_class_install_property (object_class, PROP_START_INDIVIDUAL,
      g_param_spec_object ("start-individual",
          "Start Individual",
          "The #FolksIndividual to link other individuals to.",
          FOLKS_TYPE_INDIVIDUAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * EmpathyIndividualLinker:has-changed:
   *
   * Whether #FolksIndividual<!-- -->s have been added to or removed from
   * the linked individual currently displayed in the widget.
   *
   * This will be %FALSE after the widget is initialised, and set to %TRUE when
   * an individual is checked in the individual view on the left of the widget.
   * If the individual is later unchecked, this will be reset to %FALSE, etc.
   */
  g_object_class_install_property (object_class, PROP_HAS_CHANGED,
      g_param_spec_boolean ("has-changed",
          "Changed?",
          "Whether individuals have been added to or removed from the linked "
          "individual currently displayed in the widget.",
          FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (object_class, sizeof (EmpathyIndividualLinkerPriv));
}

/**
 * empathy_individual_linker_new:
 * @start_individual: (allow-none): the #FolksIndividual to link to, or %NULL
 *
 * Creates a new #EmpathyIndividualLinker.
 *
 * Return value: a new #EmpathyIndividualLinker
 */
GtkWidget *
empathy_individual_linker_new (FolksIndividual *start_individual)
{
  g_return_val_if_fail (start_individual == NULL ||
      FOLKS_IS_INDIVIDUAL (start_individual), NULL);

  return g_object_new (EMPATHY_TYPE_INDIVIDUAL_LINKER,
      "start-individual", start_individual,
      NULL);
}

/**
 * empathy_individual_linker_get_start_individual:
 * @self: an #EmpathyIndividualLinker
 *
 * Get the value of #EmpathyIndividualLinker:start-individual.
 *
 * Return value: (transfer none): the start individual for linking, or %NULL
 */
FolksIndividual *
empathy_individual_linker_get_start_individual (EmpathyIndividualLinker *self)
{
  g_return_val_if_fail (EMPATHY_IS_INDIVIDUAL_LINKER (self), NULL);

  return GET_PRIV (self)->start_individual;
}

/**
 * empathy_individual_linker_set_start_individual:
 * @self: an #EmpathyIndividualLinker
 * @individual: (allow-none): the start individual, or %NULL
 *
 * Set the value of #EmpathyIndividualLinker:start-individual to @individual.
 */
void
empathy_individual_linker_set_start_individual (EmpathyIndividualLinker *self,
    FolksIndividual *individual)
{
  EmpathyIndividualLinkerPriv *priv;

  g_return_if_fail (EMPATHY_IS_INDIVIDUAL_LINKER (self));
  g_return_if_fail (individual == NULL || FOLKS_IS_INDIVIDUAL (individual));

  priv = GET_PRIV (self);

  tp_clear_object (&priv->start_individual);
  tp_clear_object (&priv->new_individual);
  g_hash_table_remove_all (priv->changed_individuals);

  if (individual != NULL)
    {
      priv->start_individual = g_object_ref (individual);
      priv->new_individual = folks_individual_new (
          folks_individual_get_personas (individual));
      empathy_individual_view_set_store (priv->individual_view,
          priv->individual_store);
    }
  else
    {
      priv->start_individual = NULL;
      priv->new_individual = NULL;

      /* We only display Individuals in the individual view if we have a
       * new_individual to link them into */
      empathy_individual_view_set_store (priv->individual_view, NULL);
    }

  empathy_individual_widget_set_individual (
      EMPATHY_INDIVIDUAL_WIDGET (priv->preview_widget), priv->new_individual);
  empathy_persona_store_set_individual (priv->persona_store,
      priv->new_individual);

  g_object_freeze_notify (G_OBJECT (self));
  g_object_notify (G_OBJECT (self), "start-individual");
  g_object_notify (G_OBJECT (self), "has-changed");
  g_object_thaw_notify (G_OBJECT (self));
}

/**
 * empathy_individual_linker_get_linked_personas:
 * @self: an #EmpathyIndividualLinker
 *
 * Return a list of the #FolksPersona<!-- -->s which comprise the linked
 * individual currently displayed in the widget.
 *
 * The return value is guaranteed to contain at least one element.
 *
 * Return value: (transfer none) (element-type Folks.Persona): a set of
 * #FolksPersona<!-- -->s to link together
 */
GeeSet *
empathy_individual_linker_get_linked_personas (EmpathyIndividualLinker *self)
{
  EmpathyIndividualLinkerPriv *priv;
  GeeSet *personas;

  g_return_val_if_fail (EMPATHY_IS_INDIVIDUAL_LINKER (self), NULL);

  priv = GET_PRIV (self);

  if (priv->new_individual == NULL)
    return NULL;

  personas = folks_individual_get_personas (priv->new_individual);
  g_assert (personas != NULL);
  return personas;
}

/**
 * empathy_individual_linker_get_has_changed:
 * @self: an #EmpathyIndividualLinker
 *
 * Return whether #FolksIndividual<!-- -->s have been added to or removed from
 * the linked individual currently displayed in the widget.
 *
 * This will be %FALSE after the widget is initialised, and set to %TRUE when
 * an individual is checked in the individual view on the left of the widget.
 * If the individual is later unchecked, this will be reset to %FALSE, etc.
 *
 * Return value: %TRUE if the linked individual has been changed, %FALSE
 * otherwise
 */
gboolean
empathy_individual_linker_get_has_changed (EmpathyIndividualLinker *self)
{
  EmpathyIndividualLinkerPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_INDIVIDUAL_LINKER (self), FALSE);

  priv = GET_PRIV (self);

  return (g_hash_table_size (priv->changed_individuals) > 0) ? TRUE : FALSE;
}

void
empathy_individual_linker_set_search_text (EmpathyIndividualLinker *self,
    const gchar *search_text)
{
  g_return_if_fail (EMPATHY_IS_INDIVIDUAL_LINKER (self));

  empathy_live_search_set_text (
      EMPATHY_LIVE_SEARCH (GET_PRIV (self)->search_widget), search_text);
}
