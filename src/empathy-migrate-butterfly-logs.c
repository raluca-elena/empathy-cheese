/*
*  Copyright (C) 2010 Collabora Ltd.
*
*  This library is free software; you can redistribute it and/or
*  modify it under the terms of the GNU Lesser General Public
*  License as published by the Free Software Foundation; either
*  version 2.1 of the License, or (at your option) any later version.
*
*  This library is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*  Lesser General Public License for more details.
*
*  You should have received a copy of the GNU Lesser General Public
*  License along with this library; if not, write to the Free Software
*  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <string.h>

#include <gio/gio.h>

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>
#include <libempathy/empathy-gsettings.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/defs.h>

#include "empathy-migrate-butterfly-logs.h"

static guint butterfly_log_migration_id = 0;

static void
migrate_log_files_in_dir (const gchar *dirname)
{
  GDir *dir;
  const gchar *subdir;
  gchar *new_name;
  gchar *full_path;
  GError *error = NULL;

  dir = g_dir_open (dirname, 0, &error);

  if (dir == NULL)
    {
      DEBUG ("Failed to open dir: %s", error->message);
      g_error_free (error);
      return;
    }

  while ((subdir = g_dir_read_name (dir)) != NULL)
    {
      GFile *old_gfile, *new_gfile;

      if (!tp_strdiff (subdir, "chatrooms"))
        continue;

      if (g_str_has_suffix (subdir, "#1"))
        {
          new_name = g_strndup (subdir, (strlen (subdir) - 2));
        }
      else if (g_str_has_suffix (subdir, "#32"))
        {
          gchar *tmp;
          tmp = g_strndup (subdir, (strlen (subdir) - 3));
          new_name = g_strdup_printf ("%s#yahoo", tmp);
          g_free (tmp);
        }
      else
        {
          continue;
        }

      full_path = g_build_filename (dirname, subdir, NULL);
      old_gfile = g_file_new_for_path (full_path);
      g_free (full_path);

      full_path = g_build_filename (dirname, new_name, NULL);
      new_gfile = g_file_new_for_path (full_path);
      g_free (full_path);

      if (!g_file_move (old_gfile, new_gfile, G_FILE_COPY_NONE,
              NULL, NULL, NULL, &error))
        {
          DEBUG ("Failed to move file: %s", error->message);
          g_clear_error (&error);
        }
      else
        {
          DEBUG ("Successfully migrated logs for %s", new_name);
        }

      g_free (new_name);
      g_object_unref (old_gfile);
      g_object_unref (new_gfile);
    }

  g_dir_close (dir);
}

/* This is copied from empathy-log-store-empathy.c (see #613437) */
static gchar *
log_store_account_to_dirname (TpAccount *account)
{
  const gchar *name;

  name = tp_proxy_get_object_path (account);
  if (g_str_has_prefix (name, TP_ACCOUNT_OBJECT_PATH_BASE))
    name += strlen (TP_ACCOUNT_OBJECT_PATH_BASE);

  return g_strdelimit (g_strdup (name), "/", '_');
}

static gchar *
get_log_dir_for_account (TpAccount *account)
{
  gchar *basedir;
  gchar *escaped;

  escaped = log_store_account_to_dirname (account);

  basedir = g_build_path (G_DIR_SEPARATOR_S, g_get_user_data_dir (),
    PACKAGE_NAME, "logs", escaped, NULL);

  g_free (escaped);

  return basedir;
}


static void
migration_account_manager_prepared_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  TpAccountManager *am = TP_ACCOUNT_MANAGER (source_object);
  GError *error = NULL;
  GList *accounts, *l;
  GSettings *gsettings;

  if (!tp_proxy_prepare_finish (am, result, &error))
    {
      DEBUG ("Failed to prepare the account manager: %s", error->message);
      g_error_free (error);
      return;
    }

  accounts = tp_account_manager_get_valid_accounts (am);

  for (l = accounts; l != NULL; l = l->next)
    {
      TpAccount *account = TP_ACCOUNT (l->data);
      gchar *dir, *cm;

      tp_account_parse_object_path (tp_proxy_get_object_path (account),
          &cm, NULL, NULL, NULL);

      if (tp_strdiff (cm, "butterfly"))
        {
          g_free (cm);
          continue;
        }

      dir = get_log_dir_for_account (account);
      DEBUG ("Migrating all logs from dir: %s", dir);

      migrate_log_files_in_dir (dir);

      g_free (cm);
      g_free (dir);
    }

  DEBUG ("Finished all migrating");

  gsettings = g_settings_new (EMPATHY_PREFS_SCHEMA);
  g_settings_set_boolean (gsettings, EMPATHY_PREFS_BUTTERFLY_LOGS_MIGRATED,
      TRUE);
  g_object_unref (gsettings);

  g_list_free (accounts);
}

static gboolean
migrate_logs (gpointer data)
{
  TpAccountManager *account_manager;

  account_manager = tp_account_manager_dup ();

  tp_proxy_prepare_async (account_manager, NULL,
      migration_account_manager_prepared_cb, NULL);

  g_object_unref (account_manager);

  return FALSE;
}

gboolean
empathy_migrate_butterfly_logs (EmpathyContact *contact)
{
  GSettings *gsettings = g_settings_new (EMPATHY_PREFS_SCHEMA);
  gchar *cm;

  /* Already in progress. */
  if (butterfly_log_migration_id != 0)
    return FALSE;

  /* Already done. */
  if (g_settings_get_boolean (gsettings, EMPATHY_PREFS_BUTTERFLY_LOGS_MIGRATED))
    return FALSE;

  tp_account_parse_object_path (
      tp_proxy_get_object_path (empathy_contact_get_account (contact)),
      &cm, NULL, NULL, NULL);

  if (tp_strdiff (cm, "butterfly"))
    {
      g_free (cm);
      return TRUE;
    }
  g_free (cm);

  if (g_str_has_suffix (empathy_contact_get_id (contact), "#32")
      || g_str_has_suffix (empathy_contact_get_id (contact), "#1"))
    return TRUE;

  /* Okay, we know a new butterfly is being used, so we should migrate its logs */
  butterfly_log_migration_id = g_idle_add_full (G_PRIORITY_LOW,
      migrate_logs, NULL, NULL);

  return FALSE;
}
