/*
 * Copyright (C) 2008, 2009 Collabora Ltd.
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
 * Authors: Pierre-Luc Beaudoin <pierre-luc.beaudoin@collabora.co.uk>
 */

#include <config.h>

#include <sys/stat.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>

#include <champlain/champlain.h>
#include <champlain-gtk/champlain-gtk.h>
#include <clutter-gtk/clutter-gtk.h>
#include <telepathy-glib/util.h>

#include <libempathy/empathy-contact.h>
#include <libempathy/empathy-contact-manager.h>
#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-location.h>

#include <libempathy-gtk/empathy-contact-menu.h>
#include <libempathy-gtk/empathy-ui-utils.h>

#include "empathy-map-view.h"

#define DEBUG_FLAG EMPATHY_DEBUG_LOCATION
#include <libempathy/empathy-debug.h>

G_DEFINE_TYPE (EmpathyMapView, empathy_map_view, GTK_TYPE_WINDOW);

#define GET_PRIV(self) ((EmpathyMapViewPriv *)((EmpathyMapView *) self)->priv)

struct _EmpathyMapViewPriv {
  EmpathyContactList *contact_list;

  GtkWidget *zoom_in;
  GtkWidget *zoom_out;
  GtkWidget *throbber;
  ChamplainView *map_view;
  ChamplainMarkerLayer *layer;
  guint timeout_id;
  /* reffed (EmpathyContact *) => borrowed (ChamplainMarker *) */
  GHashTable *markers;
  gulong members_changed_id;
};

static void
map_view_state_changed (ChamplainView *view,
    GParamSpec *gobject,
    EmpathyMapView *self)
{
  ChamplainState state;
  EmpathyMapViewPriv *priv = GET_PRIV (self);

  g_object_get (G_OBJECT (view), "state", &state, NULL);
  if (state == CHAMPLAIN_STATE_LOADING)
    {
      gtk_spinner_start (GTK_SPINNER (priv->throbber));
      gtk_widget_show (priv->throbber);
    }
  else
    {
      gtk_spinner_stop (GTK_SPINNER (priv->throbber));
      gtk_widget_hide (priv->throbber);
    }
}

static gboolean
contact_has_location (EmpathyContact *contact)
{
  GHashTable *location;

  location = empathy_contact_get_location (contact);

  if (location == NULL || g_hash_table_size (location) == 0)
    return FALSE;

  return TRUE;
}

static ClutterActor * create_marker (EmpathyMapView *window,
    EmpathyContact *contact);

static void
map_view_update_contact_position (EmpathyMapView *self,
    EmpathyContact *contact)
{
  EmpathyMapViewPriv *priv = GET_PRIV (self);
  gdouble lon, lat;
  GValue *value;
  GHashTable *location;
  ClutterActor *marker;
  gboolean has_location;

  has_location = contact_has_location (contact);

  marker = g_hash_table_lookup (priv->markers, contact);
  if (marker == NULL)
    {
      if (!has_location)
        return;

      marker = create_marker (self, contact);
    }
  else if (!has_location)
    {
      champlain_marker_animate_out (CHAMPLAIN_MARKER (marker));
      return;
    }

  location = empathy_contact_get_location (contact);

  value = g_hash_table_lookup (location, EMPATHY_LOCATION_LAT);
  if (value == NULL)
    {
      clutter_actor_hide (marker);
      return;
    }
  lat = g_value_get_double (value);

  value = g_hash_table_lookup (location, EMPATHY_LOCATION_LON);
  if (value == NULL)
    {
      clutter_actor_hide (marker);
      return;
    }
  lon = g_value_get_double (value);

  champlain_location_set_location (CHAMPLAIN_LOCATION (marker), lat, lon);
  champlain_marker_animate_in (CHAMPLAIN_MARKER (marker));
}

static void
map_view_contact_location_notify (EmpathyContact *contact,
    GParamSpec *arg1,
    EmpathyMapView *self)
{
  map_view_update_contact_position (self, contact);
}

static void
map_view_zoom_in_cb (GtkWidget *widget,
    EmpathyMapView *self)
{
  EmpathyMapViewPriv *priv = GET_PRIV (self);

  champlain_view_zoom_in (priv->map_view);
}

static void
map_view_zoom_out_cb (GtkWidget *widget,
    EmpathyMapView *self)
{
  EmpathyMapViewPriv *priv = GET_PRIV (self);

  champlain_view_zoom_out (priv->map_view);
}

static void
map_view_zoom_fit_cb (GtkWidget *widget,
    EmpathyMapView *self)
{
  EmpathyMapViewPriv *priv = GET_PRIV (self);

  champlain_view_ensure_layers_visible (priv->map_view, TRUE);
}

static gboolean
marker_clicked_cb (ChamplainMarker *marker,
    ClutterButtonEvent *event,
    EmpathyMapView *self)
{
  GtkWidget *menu;
  EmpathyContact *contact;

  if (event->button != 3)
    return FALSE;

  contact = g_object_get_data (G_OBJECT (marker), "contact");

  menu = empathy_contact_menu_new (contact,
      EMPATHY_CONTACT_FEATURE_CHAT |
      EMPATHY_CONTACT_FEATURE_CALL |
      EMPATHY_CONTACT_FEATURE_LOG |
      EMPATHY_CONTACT_FEATURE_FT |
      EMPATHY_CONTACT_FEATURE_INFO);

  if (menu == NULL)
    return FALSE;

  gtk_menu_attach_to_widget (GTK_MENU (menu), GTK_WIDGET (self), NULL);

  gtk_widget_show (menu);
  gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
      event->button, event->time);

  return FALSE;
}

static void
map_view_contacts_update_label (ClutterActor *marker)
{
  const gchar *name;
  gchar *date;
  gchar *label;
  GValue *gtime;
  gint64 loctime;
  GHashTable *location;
  EmpathyContact *contact;

  contact = g_object_get_data (G_OBJECT (marker), "contact");
  location = empathy_contact_get_location (contact);
  name = empathy_contact_get_alias (contact);
  gtime = g_hash_table_lookup (location, EMPATHY_LOCATION_TIMESTAMP);

  if (gtime != NULL)
    {
      GDateTime *now, *d;
      GTimeSpan delta;

      loctime = g_value_get_int64 (gtime);
      date = empathy_time_to_string_relative (loctime);
      label = g_strconcat ("<b>", name, "</b>\n<small>", date, "</small>", NULL);
      g_free (date);

      now = g_date_time_new_now_utc ();
      d = g_date_time_new_from_unix_utc (loctime);
      delta = g_date_time_difference (now, d);

      /* if location is older than a week */
      if (delta > G_TIME_SPAN_DAY * 7)
        clutter_actor_set_opacity (marker, 0.75 * 255);

      g_date_time_unref (now);
      g_date_time_unref (d);
    }
  else
    {
      label = g_strconcat ("<b>", name, "</b>\n", NULL);
    }

  champlain_label_set_use_markup (CHAMPLAIN_LABEL (marker), TRUE);
  champlain_label_set_text (CHAMPLAIN_LABEL (marker), label);

  g_free (label);
}

static ClutterActor *
create_marker (EmpathyMapView *self,
    EmpathyContact *contact)
{
  EmpathyMapViewPriv *priv = GET_PRIV (self);
  ClutterActor *marker;
  GdkPixbuf *avatar;
  ClutterActor *texture = NULL;

  avatar = empathy_pixbuf_avatar_from_contact_scaled (contact, 32, 32);
  if (avatar != NULL)
    {
      texture = gtk_clutter_texture_new ();

      gtk_clutter_texture_set_from_pixbuf (GTK_CLUTTER_TEXTURE (texture),
          avatar, NULL);

      g_object_unref (avatar);
    }

  marker = champlain_label_new_with_image (texture);

  g_object_set_data_full (G_OBJECT (marker), "contact",
      g_object_ref (contact), g_object_unref);

  g_hash_table_insert (priv->markers, g_object_ref (contact), marker);

  map_view_contacts_update_label (marker);

  clutter_actor_set_reactive (CLUTTER_ACTOR (marker), TRUE);
  g_signal_connect (marker, "button-release-event",
      G_CALLBACK (marker_clicked_cb), self);

  champlain_marker_layer_add_marker (priv->layer, CHAMPLAIN_MARKER (marker));

  DEBUG ("Create marker for %s", empathy_contact_get_id (contact));

  tp_clear_object (&texture);
  return marker;
}

static void
contact_added (EmpathyMapView *self,
    EmpathyContact *contact)
{
  g_signal_connect (contact, "notify::location",
      G_CALLBACK (map_view_contact_location_notify), self);

  map_view_update_contact_position (self, contact);
}

static gboolean
map_view_key_press_cb (GtkWidget *widget,
    GdkEventKey *event,
    gpointer user_data)
{
  if ((event->state & GDK_CONTROL_MASK && event->keyval == GDK_KEY_w)
      || event->keyval == GDK_KEY_Escape)
    {
      gtk_widget_destroy (widget);
      return TRUE;
    }

  return FALSE;
}

static gboolean
map_view_tick (EmpathyMapView *self)
{
  EmpathyMapViewPriv *priv = GET_PRIV (self);
  GList *marker, *l;

  marker = champlain_marker_layer_get_markers (priv->layer);

  for (l = marker; l != NULL; l = g_list_next (l))
    map_view_contacts_update_label (l->data);

  g_list_free (marker);
  return TRUE;
}

static void
contact_removed (EmpathyMapView *self,
    EmpathyContact *contact)
{
  EmpathyMapViewPriv *priv = GET_PRIV (self);
  ClutterActor *marker;

  marker = g_hash_table_lookup (priv->markers, contact);
  if (marker == NULL)
    return;

  clutter_actor_destroy (marker);
  g_hash_table_remove (priv->markers, contact);
}

static void
members_changed_cb (EmpathyContactList *list,
    EmpathyContact *contact,
    EmpathyContact *actor,
    guint reason,
    gchar *message,
    gboolean is_member,
    EmpathyMapView *self)
{
  if (is_member)
    {
      contact_added (self, contact);
    }
  else
    {
      contact_removed (self, contact);
    }
}

static GObject *
empathy_map_view_constructor (GType type,
    guint n_construct_params,
    GObjectConstructParam *construct_params)
{
  static GObject *window = NULL;

  if (window != NULL)
    return window;

  window = G_OBJECT_CLASS (empathy_map_view_parent_class)->constructor (
      type, n_construct_params, construct_params);

  g_object_add_weak_pointer (window, (gpointer) &window);

  return window;
}

static void
empathy_map_view_finalize (GObject *object)
{
  EmpathyMapViewPriv *priv = GET_PRIV (object);
  GHashTableIter iter;
  gpointer contact;

  g_source_remove (priv->timeout_id);

  g_hash_table_iter_init (&iter, priv->markers);
  while (g_hash_table_iter_next (&iter, &contact, NULL))
    g_signal_handlers_disconnect_by_func (contact,
        map_view_contact_location_notify, object);

  g_signal_handler_disconnect (priv->contact_list,
      priv->members_changed_id);

  g_hash_table_unref (priv->markers);
  g_object_unref (priv->contact_list);
  g_object_unref (priv->layer);

  G_OBJECT_CLASS (empathy_map_view_parent_class)->finalize (object);
}

static void
empathy_map_view_class_init (EmpathyMapViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructor = empathy_map_view_constructor;
  object_class->finalize = empathy_map_view_finalize;

  g_type_class_add_private (object_class, sizeof (EmpathyMapViewPriv));
}

static void
empathy_map_view_init (EmpathyMapView *self)
{
  EmpathyMapViewPriv *priv;
  GtkBuilder *gui;
  GtkWidget *sw;
  GtkWidget *embed;
  GtkWidget *throbber_holder;
  gchar *filename;
  GList *members, *l;
  GtkWidget *main_vbox;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_MAP_VIEW, EmpathyMapViewPriv);

  gtk_window_set_title (GTK_WINDOW (self), _("Contact Map View"));
  gtk_window_set_role (GTK_WINDOW (self), "map_view");
  gtk_window_set_default_size (GTK_WINDOW (self), 512, 384);
  gtk_window_set_position (GTK_WINDOW (self), GTK_WIN_POS_CENTER);

  /* Set up interface */
  filename = empathy_file_lookup ("empathy-map-view.ui", "src");
  gui = empathy_builder_get_file (filename,
     "main_vbox", &main_vbox,
     "zoom_in", &priv->zoom_in,
     "zoom_out", &priv->zoom_out,
     "map_scrolledwindow", &sw,
     "throbber", &throbber_holder,
     NULL);
  g_free (filename);

  gtk_container_add (GTK_CONTAINER (self), main_vbox);

  empathy_builder_connect (gui, self,
      "zoom_in", "clicked", map_view_zoom_in_cb,
      "zoom_out", "clicked", map_view_zoom_out_cb,
      "zoom_fit", "clicked", map_view_zoom_fit_cb,
      NULL);

  g_signal_connect (self, "key-press-event",
      G_CALLBACK (map_view_key_press_cb), self);

  g_object_unref (gui);

  priv->contact_list = EMPATHY_CONTACT_LIST (
      empathy_contact_manager_dup_singleton ());

  priv->members_changed_id = g_signal_connect (priv->contact_list,
      "members-changed", G_CALLBACK (members_changed_cb), self);

  priv->throbber = gtk_spinner_new ();
  gtk_widget_set_size_request (priv->throbber, 16, 16);
  gtk_container_add (GTK_CONTAINER (throbber_holder), priv->throbber);

  /* Set up map view */
  embed = gtk_champlain_embed_new ();
  priv->map_view = gtk_champlain_embed_get_view (GTK_CHAMPLAIN_EMBED (embed));
  g_object_set (G_OBJECT (priv->map_view),
     "zoom-level", 1,
     "kinetic-mode", TRUE,
     NULL);
  champlain_view_center_on (priv->map_view, 36, 0);

  gtk_container_add (GTK_CONTAINER (sw), embed);
  gtk_widget_show_all (embed);

  priv->layer = g_object_ref (champlain_marker_layer_new ());
  champlain_view_add_layer (priv->map_view, CHAMPLAIN_LAYER (priv->layer));

  g_signal_connect (priv->map_view, "notify::state",
      G_CALLBACK (map_view_state_changed), self);

  /* Set up contact list. */
  priv->markers = g_hash_table_new_full (NULL, NULL,
      (GDestroyNotify) g_object_unref, NULL);

  members = empathy_contact_list_get_members (
      priv->contact_list);
  for (l = members; l != NULL; l = g_list_next (l))
    {
      contact_added (self, l->data);
      g_object_unref (l->data);
    }
  g_list_free (members);

  /* Set up time updating loop */
  priv->timeout_id = g_timeout_add_seconds (5,
      (GSourceFunc) map_view_tick, self);
}

GtkWidget *
empathy_map_view_show (void)
{
  GtkWidget *window;

  window = g_object_new (EMPATHY_TYPE_MAP_VIEW, NULL);
  gtk_widget_show_all (window);
  empathy_window_present (GTK_WINDOW (window));

  return window;
}
