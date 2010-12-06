/*
 * Copyright (C) 2010 Collabora Ltd.
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

#include "config.h"

#include "empathy-keyring.h"

#include <string.h>

#include <gnome-keyring.h>

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include "empathy-debug.h"

/*
GnomeKeyringPasswordSchema keyring_schema =
  { GNOME_KEYRING_ITEM_GENERIC_SECRET,
    { { "account", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
      { "param", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
      { NULL } } };
*/

static void
find_items_cb (GnomeKeyringResult result,
    GList *list,
    gpointer user_data)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);

  if (result != GNOME_KEYRING_RESULT_OK)
    {
      GError error = { TP_ERROR, TP_ERROR_DOES_NOT_EXIST,
                       (gchar *) gnome_keyring_result_to_message (result) };
      g_simple_async_result_set_from_error (simple, &error);
    }

  if (g_list_length (list) == 1)
    {
      GnomeKeyringFound *found = list->data;

      DEBUG ("Got secret");

      g_simple_async_result_set_op_res_gpointer (simple, found->secret, NULL);
    }

  g_simple_async_result_complete (simple);
  g_object_unref (simple);
}

void
empathy_keyring_get_password_async (TpAccount *account,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *simple;
  GnomeKeyringAttributeList *match;
  const gchar *account_id;

  g_return_if_fail (TP_IS_ACCOUNT (account));
  g_return_if_fail (callback != NULL);

  simple = g_simple_async_result_new (G_OBJECT (account), callback,
      user_data, empathy_keyring_get_password_async);

  account_id = tp_proxy_get_object_path (account) +
    strlen (TP_ACCOUNT_OBJECT_PATH_BASE);

  DEBUG ("Trying to get password for: %s", account_id);

  match = gnome_keyring_attribute_list_new ();
  gnome_keyring_attribute_list_append_string (match, "account",
      account_id);
  gnome_keyring_attribute_list_append_string (match, "param", "password");

  gnome_keyring_find_items (GNOME_KEYRING_ITEM_GENERIC_SECRET,
      match, find_items_cb, simple, NULL);

  gnome_keyring_attribute_list_free (match);
}

const gchar *
empathy_keyring_get_password_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (TP_IS_ACCOUNT (account), NULL);
  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), NULL);

  simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return NULL;

  g_return_val_if_fail (g_simple_async_result_is_valid (result,
          G_OBJECT (account), empathy_keyring_get_password_async), NULL);

  return g_simple_async_result_get_op_res_gpointer (simple);
}
