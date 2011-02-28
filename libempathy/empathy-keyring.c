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

#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include "empathy-debug.h"

static GnomeKeyringPasswordSchema account_keyring_schema =
  { GNOME_KEYRING_ITEM_GENERIC_SECRET,
    { { "account-id", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
      { "param-name", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
      { NULL } } };

static GnomeKeyringPasswordSchema room_keyring_schema =
  { GNOME_KEYRING_ITEM_GENERIC_SECRET,
    { { "account-id", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
      { "room-id", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
      { NULL } } };

gboolean
empathy_keyring_is_available (void)
{
  return gnome_keyring_is_available ();
}

/* get */

static void
find_items_cb (GnomeKeyringResult result,
    GList *list,
    gpointer user_data)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
  GnomeKeyringFound *found;

  if (result != GNOME_KEYRING_RESULT_OK)
    {
      GError *error = g_error_new_literal (TP_ERROR,
          TP_ERROR_DOES_NOT_EXIST,
          gnome_keyring_result_to_message (result));
      g_simple_async_result_set_from_error (simple, error);
      g_clear_error (&error);
      goto out;
    }

  if (list == NULL)
    {
      g_simple_async_result_set_error (simple, TP_ERROR,
          TP_ERROR_DOES_NOT_EXIST, "Password not found");
      goto out;
    }

  /* Get the first password returned. Ideally we should use the latest
   * modified or something but we don't have this information from
   * gnome-keyring atm. */
  found = list->data;
  DEBUG ("Got secret");

  g_simple_async_result_set_op_res_gpointer (simple, g_strdup (found->secret),
      g_free);

out:
  g_simple_async_result_complete (simple);
  g_object_unref (simple);
}

void
empathy_keyring_get_account_password_async (TpAccount *account,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *simple;
  GnomeKeyringAttributeList *match;
  const gchar *account_id;

  g_return_if_fail (TP_IS_ACCOUNT (account));
  g_return_if_fail (callback != NULL);

  simple = g_simple_async_result_new (G_OBJECT (account), callback,
      user_data, empathy_keyring_get_account_password_async);

  account_id = tp_proxy_get_object_path (account) +
    strlen (TP_ACCOUNT_OBJECT_PATH_BASE);

  DEBUG ("Trying to get password for: %s", account_id);

  match = gnome_keyring_attribute_list_new ();
  gnome_keyring_attribute_list_append_string (match, "account-id",
      account_id);
  gnome_keyring_attribute_list_append_string (match, "param-name", "password");

  gnome_keyring_find_items (GNOME_KEYRING_ITEM_GENERIC_SECRET,
      match, find_items_cb, simple, NULL);

  gnome_keyring_attribute_list_free (match);
}

void
empathy_keyring_get_room_password_async (TpAccount *account,
    const gchar *id,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *simple;
  GnomeKeyringAttributeList *match;
  const gchar *account_id;

  g_return_if_fail (TP_IS_ACCOUNT (account));
  g_return_if_fail (id != NULL);
  g_return_if_fail (callback != NULL);

  simple = g_simple_async_result_new (G_OBJECT (account), callback,
      user_data, empathy_keyring_get_room_password_async);

  account_id = tp_proxy_get_object_path (account) +
    strlen (TP_ACCOUNT_OBJECT_PATH_BASE);

  DEBUG ("Trying to get password for room '%s' on account '%s'",
      id, account_id);

  match = gnome_keyring_attribute_list_new ();
  gnome_keyring_attribute_list_append_string (match, "account-id",
      account_id);
  gnome_keyring_attribute_list_append_string (match, "room-id", id);

  gnome_keyring_find_items (GNOME_KEYRING_ITEM_GENERIC_SECRET,
      match, find_items_cb, simple, NULL);

  gnome_keyring_attribute_list_free (match);
}

const gchar *
empathy_keyring_get_account_password_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  empathy_implement_finish_return_pointer (account,
      empathy_keyring_get_account_password_async);
}

const gchar *
empathy_keyring_get_room_password_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  empathy_implement_finish_return_pointer (account,
      empathy_keyring_get_room_password_async);
}

/* set */

static void
store_password_cb (GnomeKeyringResult result,
    gpointer user_data)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);

  if (result != GNOME_KEYRING_RESULT_OK)
    {
      GError *error = g_error_new_literal (TP_ERROR,
          TP_ERROR_DOES_NOT_EXIST,
          gnome_keyring_result_to_message (result));
      g_simple_async_result_set_from_error (simple, error);
      g_clear_error (&error);
    }

  g_simple_async_result_complete (simple);
  g_object_unref (simple);
}

void
empathy_keyring_set_account_password_async (TpAccount *account,
    const gchar *password,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *simple;
  const gchar *account_id;
  gchar *name;

  g_return_if_fail (TP_IS_ACCOUNT (account));
  g_return_if_fail (password != NULL);

  simple = g_simple_async_result_new (G_OBJECT (account), callback,
      user_data, empathy_keyring_set_account_password_async);

  account_id = tp_proxy_get_object_path (account) +
    strlen (TP_ACCOUNT_OBJECT_PATH_BASE);

  DEBUG ("Remembering password for %s", account_id);

  name = g_strdup_printf ("IM account password for %s (%s)",
      tp_account_get_display_name (account), account_id);

  gnome_keyring_store_password (&account_keyring_schema, NULL, name, password,
      store_password_cb, simple, NULL,
      "account-id", account_id,
      "param-name", "password",
      NULL);

  g_free (name);
}

void
empathy_keyring_set_room_password_async (TpAccount *account,
    const gchar *id,
    const gchar *password,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *simple;
  const gchar *account_id;
  gchar *name;

  g_return_if_fail (TP_IS_ACCOUNT (account));
  g_return_if_fail (id != NULL);
  g_return_if_fail (password != NULL);

  simple = g_simple_async_result_new (G_OBJECT (account), callback,
      user_data, empathy_keyring_set_room_password_async);

  account_id = tp_proxy_get_object_path (account) +
    strlen (TP_ACCOUNT_OBJECT_PATH_BASE);

  DEBUG ("Remembering password for room '%s' on account '%s'", id, account_id);

  name = g_strdup_printf ("Password for chatroom '%s' on account %s (%s)",
      id, tp_account_get_display_name (account), account_id);

  gnome_keyring_store_password (&room_keyring_schema, NULL, name, password,
      store_password_cb, simple, NULL,
      "account-id", account_id,
      "room-id", id,
      NULL);

  g_free (name);
}

gboolean
empathy_keyring_set_account_password_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  empathy_implement_finish_void (account, empathy_keyring_set_account_password_async);
}

gboolean
empathy_keyring_set_room_password_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  empathy_implement_finish_void (account, empathy_keyring_set_room_password_async);
}

/* delete */

static void
item_delete_cb (GnomeKeyringResult result,
    gpointer user_data)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);

  if (result != GNOME_KEYRING_RESULT_OK)
    {
      GError *error = g_error_new_literal (TP_ERROR,
          TP_ERROR_DOES_NOT_EXIST,
          gnome_keyring_result_to_message (result));
      g_simple_async_result_set_from_error (simple, error);
      g_clear_error (&error);
    }

  g_simple_async_result_complete (simple);
  g_object_unref (simple);
}

static void
find_item_to_delete_cb (GnomeKeyringResult result,
    GList *list,
    gpointer user_data)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (user_data);
  GnomeKeyringFound *found;

  if (result != GNOME_KEYRING_RESULT_OK || g_list_length (list) != 1)
    {
      GError *error = g_error_new_literal (TP_ERROR,
          TP_ERROR_DOES_NOT_EXIST,
          gnome_keyring_result_to_message (result));
      g_simple_async_result_set_from_error (simple, error);
      g_clear_error (&error);

      g_simple_async_result_complete (simple);
      g_object_unref (simple);
      return;
    }

  found = list->data;

  gnome_keyring_item_delete (NULL, found->item_id, item_delete_cb,
      simple, NULL);
}

void
empathy_keyring_delete_account_password_async (TpAccount *account,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *simple;
  GnomeKeyringAttributeList *match;
  const gchar *account_id;

  g_return_if_fail (TP_IS_ACCOUNT (account));

  simple = g_simple_async_result_new (G_OBJECT (account), callback,
      user_data, empathy_keyring_delete_account_password_async);

  account_id = tp_proxy_get_object_path (account) +
    strlen (TP_ACCOUNT_OBJECT_PATH_BASE);

  match = gnome_keyring_attribute_list_new ();
  gnome_keyring_attribute_list_append_string (match, "account-id",
      account_id);
  gnome_keyring_attribute_list_append_string (match, "param-name", "password");

  gnome_keyring_find_items (GNOME_KEYRING_ITEM_GENERIC_SECRET,
      match, find_item_to_delete_cb, simple, NULL);

  gnome_keyring_attribute_list_free (match);
}

gboolean
empathy_keyring_delete_account_password_finish (TpAccount *account,
    GAsyncResult *result,
    GError **error)
{
  empathy_implement_finish_void (account, empathy_keyring_delete_account_password_async);
}

