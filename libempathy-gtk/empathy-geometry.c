/*
 * Copyright (C) 2006-2007 Imendio AB
 * Copyright (C) 2007-2008 Collabora Ltd.
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
 */

#include "config.h"

#include <sys/stat.h>

#include <glib.h>
#include <gdk/gdk.h>

#include "libempathy/empathy-utils.h"
#include "empathy-geometry.h"
#include "empathy-ui-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

#define GEOMETRY_DIR_CREATE_MODE  (S_IRUSR | S_IWUSR | S_IXUSR)
#define GEOMETRY_FILE_CREATE_MODE (S_IRUSR | S_IWUSR)

/* geometry.ini file contains 2 groups:
 *  - one with position and size of each window
 *  - one with the maximized state of each window
 * Windows are identified by a name. (e.g. "main-window") */
#define GEOMETRY_FILENAME             "geometry.ini"
#define GEOMETRY_POSITION_FORMAT      "%d,%d,%d,%d" /* "x,y,w,h" */
#define GEOMETRY_POSITION_GROUP       "geometry"
#define GEOMETRY_MAXIMIZED_GROUP      "maximized"

/* Key used to keep window's name inside the object's qdata */
#define GEOMETRY_NAME_KEY             "geometry-name-key"

static guint store_id = 0;

static void
geometry_real_store (GKeyFile *key_file)
{
  gchar *filename;
  gchar *content;
  gsize length;
  GError *error = NULL;

  content = g_key_file_to_data (key_file, &length, &error);
  if (error != NULL)
    {
      DEBUG ("Error: %s", error->message);
      g_error_free (error);
      return;
    }

  filename = g_build_filename (g_get_user_config_dir (),
    PACKAGE_NAME, GEOMETRY_FILENAME, NULL);

  if (!g_file_set_contents (filename, content, length, &error))
    {
      DEBUG ("Error: %s", error->message);
      g_error_free (error);
    }

  g_free (content);
  g_free (filename);
}

static gboolean
geometry_store_cb (gpointer key_file)
{
  geometry_real_store (key_file);
  store_id = 0;

  return FALSE;
}

static void
geometry_schedule_store (GKeyFile *key_file)
{
  if (store_id != 0)
    g_source_remove (store_id);

  store_id = g_timeout_add_seconds (1, geometry_store_cb, key_file);
}

static GKeyFile *
geometry_get_key_file (void)
{
  static GKeyFile *key_file = NULL;
  gchar *dir;
  gchar *filename;

  if (key_file != NULL)
    return key_file;

  dir = g_build_filename (g_get_user_config_dir (), PACKAGE_NAME, NULL);
  if (!g_file_test (dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
    {
      DEBUG ("Creating directory:'%s'", dir);
      g_mkdir_with_parents (dir, GEOMETRY_DIR_CREATE_MODE);
    }

  filename = g_build_filename (dir, GEOMETRY_FILENAME, NULL);
  g_free (dir);

  key_file = g_key_file_new ();
  g_key_file_load_from_file (key_file, filename, G_KEY_FILE_NONE, NULL);
  g_free (filename);

  return key_file;
}

void
empathy_geometry_save_values (GtkWindow *window,
    gint x,
    gint y,
    gint w,
    gint h,
    gboolean maximized)
{
  GKeyFile *key_file;
  gchar *position_str = NULL;
  GHashTable *names;
  GHashTableIter iter;
  const gchar *name;

  names = g_object_get_data (G_OBJECT (window), GEOMETRY_NAME_KEY);

  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (names != NULL);

  /* Don't save off-screen positioning */
  if (!EMPATHY_RECT_IS_ON_SCREEN (x, y, w, h))
    return;

  key_file = geometry_get_key_file ();

  /* Save window size only if not maximized */
  if (!maximized)
    {
      position_str = g_strdup_printf (GEOMETRY_POSITION_FORMAT, x, y, w, h);
    }

  g_hash_table_iter_init (&iter, names);
  while (g_hash_table_iter_next (&iter, (gpointer) &name, NULL))
    {
      gchar *escaped_name;

      /* escape the name so that unwanted characters such as # are removed */
      escaped_name = g_uri_escape_string (name, NULL, TRUE);

      g_key_file_set_boolean (key_file, GEOMETRY_MAXIMIZED_GROUP,
          escaped_name, maximized);

      if (position_str != NULL)
        g_key_file_set_string (key_file, GEOMETRY_POSITION_GROUP,
            escaped_name, position_str);

      g_free (escaped_name);
    }


  geometry_schedule_store (key_file);
  g_free (position_str);
}

static void
empathy_geometry_save (GtkWindow *window)
{
  GdkWindow *gdk_window;
  GdkWindowState window_state;
  gboolean maximized;
  gint x, y, w, h;

  g_return_if_fail (GTK_IS_WINDOW (window));

  if (!gtk_widget_get_visible (GTK_WIDGET (window)))
    return;

  /* Get window geometry */
  gtk_window_get_position (window, &x, &y);
  gtk_window_get_size (window, &w, &h);

  gdk_window = gtk_widget_get_window (GTK_WIDGET (window));
  window_state = gdk_window_get_state (gdk_window);
  maximized = (window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;

  empathy_geometry_save_values (window, x, y, w, h, maximized);
}

static void
empathy_geometry_load (GtkWindow *window,
    const gchar *name)
{
  GKeyFile *key_file;
  gchar    *escaped_name;
  gchar    *str;
  gboolean  maximized;

  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (!EMP_STR_EMPTY (name));

  /* escape the name so that unwanted characters such as # are removed */
  escaped_name = g_uri_escape_string (name, NULL, TRUE);

  key_file = geometry_get_key_file ();

  /* restore window size and position */
  str = g_key_file_get_string (key_file, GEOMETRY_POSITION_GROUP,
      escaped_name, NULL);
  if (str)
    {
      gint x, y, w, h;

      sscanf (str, GEOMETRY_POSITION_FORMAT, &x, &y, &w, &h);
      gtk_window_move (window, x, y);
      gtk_window_resize (window, w, h);
    }

  /* restore window maximized state */
  maximized = g_key_file_get_boolean (key_file, GEOMETRY_MAXIMIZED_GROUP,
      escaped_name, NULL);

  if (maximized)
    gtk_window_maximize (window);
  else
    gtk_window_unmaximize (window);

  g_free (str);
  g_free (escaped_name);
}

static gboolean
geometry_configure_event_cb (GtkWindow *window,
    GdkEventConfigure *event,
    gpointer user_data)
{
  empathy_geometry_save (window);

  return FALSE;
}

static gboolean
geometry_window_state_event_cb (GtkWindow *window,
    GdkEventWindowState *event,
    gpointer user_data)
{
  if ((event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED) != 0)
    {
      empathy_geometry_save (window);
    }

  return FALSE;
}

static void
geometry_map_cb (GtkWindow *window,
    gpointer user_data)
{
  GHashTable *names;
  GHashTableIter iter;
  const gchar *name;

  /* The WM will replace this window, restore its last position */
  names = g_object_get_data (G_OBJECT (window), GEOMETRY_NAME_KEY);

  g_assert (names != NULL);

  /* Use the first name we get in the hash table */
  g_hash_table_iter_init (&iter, names);
  g_assert (g_hash_table_iter_next (&iter, (gpointer) &name, NULL));

  empathy_geometry_load (window, name);
}

void
empathy_geometry_bind (GtkWindow *window,
    const gchar *name)
{
  GHashTable *names;
  gboolean connect_sigs = FALSE;

  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (!EMP_STR_EMPTY (name));

  /* Check if this window is already bound */
  names = g_object_get_data (G_OBJECT (window), GEOMETRY_NAME_KEY);
  if (names == NULL)
    {
      connect_sigs = TRUE;
      names = g_hash_table_new_full (g_str_hash, g_str_equal,
          g_free, NULL);

      g_object_set_data_full (G_OBJECT (window), GEOMETRY_NAME_KEY, names,
          (GDestroyNotify) g_hash_table_unref);
    }
  else if (g_hash_table_lookup (names, name) != NULL)
    {
      return;
    }

  /* Store the geometry name in the window's data */
  g_hash_table_insert (names, g_strdup (name), GUINT_TO_POINTER (TRUE));

  /* Load initial geometry */
  empathy_geometry_load (window, name);

  if (!connect_sigs)
    return;

  /* Track geometry changes */
  g_signal_connect (window, "configure-event",
    G_CALLBACK (geometry_configure_event_cb), NULL);
  g_signal_connect (window, "window-state-event",
    G_CALLBACK (geometry_window_state_event_cb), NULL);
  g_signal_connect (window, "map",
    G_CALLBACK (geometry_map_cb), NULL);
}

void
empathy_geometry_unbind (GtkWindow *window,
    const gchar *name)
{
  GHashTable *names;

  names = g_object_get_data (G_OBJECT (window), GEOMETRY_NAME_KEY);
  if (names == NULL)
    return;

  g_hash_table_remove (names, name);

  if (g_hash_table_size (names) > 0)
    return;

  g_signal_handlers_disconnect_by_func (window,
    geometry_configure_event_cb, NULL);
  g_signal_handlers_disconnect_by_func (window,
    geometry_window_state_event_cb, NULL);
  g_signal_handlers_disconnect_by_func (window,
    geometry_map_cb, NULL);

  g_object_set_data (G_OBJECT (window), GEOMETRY_NAME_KEY, NULL);
}
