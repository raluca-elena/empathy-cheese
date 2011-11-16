/*
 * Copyright (C) 2009 Collabora Ltd.
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
 * Authors: Jonny Lamb <jonny.lamb@collabora.co.uk>
 *          Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

#include <telepathy-glib/util.h>

#include <libempathy/empathy-connection-managers.h>
#include <libempathy/empathy-utils.h>

#include "empathy-import-utils.h"
#include "empathy-import-pidgin.h"

EmpathyImportAccountData *
empathy_import_account_data_new (const gchar *source)
{
  EmpathyImportAccountData *data;

  g_return_val_if_fail (!EMP_STR_EMPTY (source), NULL);

  data = g_slice_new0 (EmpathyImportAccountData);
  data->settings = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
    (GDestroyNotify) tp_g_value_slice_free);
  data->source = g_strdup (source);
  data->protocol = NULL;
  data->connection_manager = NULL;
  data->enabled = FALSE;

  return data;
}

void
empathy_import_account_data_free (EmpathyImportAccountData *data)
{
  if (data == NULL)
    return;
  if (data->protocol != NULL)
    g_free (data->protocol);
  if (data->connection_manager != NULL)
    g_free (data->connection_manager);
  if (data->settings != NULL)
    g_hash_table_unref (data->settings);
  if (data->source != NULL)
    g_free (data->source);

  g_slice_free (EmpathyImportAccountData, data);
}

gboolean
empathy_import_accounts_to_import (void)
{
  return empathy_import_pidgin_accounts_to_import ();
}

GList *
empathy_import_accounts_load (EmpathyImportApplication id)
{
  if (id == EMPATHY_IMPORT_APPLICATION_PIDGIN)
    return empathy_import_pidgin_load ();

  return empathy_import_pidgin_load ();
}

gboolean
empathy_import_protocol_is_supported (const gchar *protocol,
    TpConnectionManager **cm)
{
  EmpathyConnectionManagers *manager;
  GList *cms;
  GList *l;
  gboolean proto_is_supported = FALSE;

  manager = empathy_connection_managers_dup_singleton ();
  cms = empathy_connection_managers_get_cms (manager);

  for (l = cms; l; l = l->next)
    {

      TpConnectionManager *tp_cm = l->data;
      if (tp_connection_manager_has_protocol (tp_cm,
          (const gchar*) protocol))
        {
          if (!tp_strdiff (protocol, "irc")
              && !tp_strdiff (tp_cm->name, "haze"))
              continue;

          if (!proto_is_supported)
            {
              *cm = tp_cm;
              proto_is_supported = TRUE;

              continue;
            }

          /* we have more than one CM for this protocol,
           * select the one which is not haze.
           */
          if (!tp_strdiff ((*cm)->name, "haze"))
            {
              *cm = tp_cm;
              break;
            }
        }
    }

  g_object_unref (manager);

  return proto_is_supported;
}
