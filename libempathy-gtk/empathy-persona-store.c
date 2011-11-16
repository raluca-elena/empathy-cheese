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
 * Based off EmpathyContactListStore.
 */

#include "config.h"

#include <string.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <telepathy-glib/util.h>

#include <folks/folks.h>
#include <folks/folks-telepathy.h>

#include <libempathy/empathy-utils.h>

#include "empathy-persona-store.h"
#include "empathy-gtk-enum-types.h"
#include "empathy-ui-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_CONTACT
#include <libempathy/empathy-debug.h>

/* Active users are those which have recently changed state
 * (e.g. online, offline or from normal to a busy state). */

/* Time in seconds user is shown as active */
#define ACTIVE_USER_SHOW_TIME 7

/* Time in seconds after connecting which we wait before active users are
 * enabled */
#define ACTIVE_USER_WAIT_TO_ENABLE_TIME 5

static void add_persona (EmpathyPersonaStore *self,
    FolksPersona *persona);
static GtkTreePath * find_persona (EmpathyPersonaStore *self,
    FolksPersona *persona);
static void update_persona (EmpathyPersonaStore *self,
    FolksPersona *persona);
static void remove_persona (EmpathyPersonaStore *self,
    FolksPersona *persona);

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyPersonaStore)

typedef struct
{
  FolksIndividual *individual; /* owned */
  GHashTable *personas; /* owned Persona -> owned GtkTreeRowReference */

  gboolean show_avatars;
  gboolean show_protocols;
  gboolean show_active;
  EmpathyPersonaStoreSort sort_criterion;

  guint inhibit_active;
  guint setup_idle_id;

  GHashTable *status_icons; /* owned icon name -> owned GdkPixbuf */
} EmpathyPersonaStorePriv;

enum {
  PROP_0,
  PROP_INDIVIDUAL,
  PROP_SHOW_AVATARS,
  PROP_SHOW_PROTOCOLS,
  PROP_SORT_CRITERION
};

G_DEFINE_TYPE (EmpathyPersonaStore, empathy_persona_store, GTK_TYPE_LIST_STORE);

static gboolean
inhibit_active_cb (EmpathyPersonaStore *store)
{
  EmpathyPersonaStorePriv *priv;

  priv = GET_PRIV (store);

  priv->show_active = TRUE;
  priv->inhibit_active = 0;

  return FALSE;
}

typedef struct {
  EmpathyPersonaStore *store;
  FolksPersona *persona;
  gboolean remove;
  guint timeout;
} ShowActiveData;

static void persona_active_free (ShowActiveData *data);

static void
persona_active_invalidated (ShowActiveData *data,
    GObject *old_object)
{
  /* Remove the timeout and free the struct, since the persona or persona
   * store has disappeared. */
  g_source_remove (data->timeout);

  if (old_object == (GObject *) data->store)
    data->store = NULL;
  else if (old_object == (GObject *) data->persona)
    data->persona = NULL;
  else
    g_assert_not_reached ();

  persona_active_free (data);
}

static ShowActiveData *
persona_active_new (EmpathyPersonaStore *self,
    FolksPersona *persona,
    gboolean remove_)
{
  ShowActiveData *data;

  DEBUG ("Contact:'%s' now active, and %s be removed",
      folks_alias_details_get_alias (FOLKS_ALIAS_DETAILS (persona)),
      remove_ ? "WILL" : "WILL NOT");

  data = g_slice_new0 (ShowActiveData);

  /* We don't actually want to force either the PersonaStore or the
   * Persona to stay alive, since the user could quit Empathy or disable
   * the account before the persona_active timeout is fired. */
  g_object_weak_ref (G_OBJECT (self),
      (GWeakNotify) persona_active_invalidated, data);
  g_object_weak_ref (G_OBJECT (persona),
      (GWeakNotify) persona_active_invalidated, data);

  data->store = self;
  data->persona = persona;
  data->remove = remove_;

  return data;
}

static void
persona_active_free (ShowActiveData *data)
{
  if (data->store != NULL)
    {
      g_object_weak_unref (G_OBJECT (data->store),
          (GWeakNotify) persona_active_invalidated, data);
    }

  if (data->persona != NULL)
    {
      g_object_weak_unref (G_OBJECT (data->persona),
          (GWeakNotify) persona_active_invalidated, data);
    }

  g_slice_free (ShowActiveData, data);
}

static void
persona_set_active (EmpathyPersonaStore *self,
    FolksPersona *persona,
    gboolean active,
    gboolean set_changed)
{
  GtkTreePath *path;
  GtkTreeIter iter;

  path = find_persona (self, persona);
  if (path == NULL)
    return;

  gtk_tree_model_get_iter (GTK_TREE_MODEL (self), &iter, path);
  gtk_list_store_set (GTK_LIST_STORE (self), &iter,
      EMPATHY_PERSONA_STORE_COL_IS_ACTIVE, active,
      -1);

  DEBUG ("Set item %s", active ? "active" : "inactive");

  if (set_changed)
    gtk_tree_model_row_changed (GTK_TREE_MODEL (self), path, &iter);

  gtk_tree_path_free (path);
}

static gboolean
persona_active_cb (ShowActiveData *data)
{
  const gchar *alias =
      folks_alias_details_get_alias (FOLKS_ALIAS_DETAILS (data->persona));

  if (data->remove)
    {
      DEBUG ("Contact:'%s' active timeout, removing item", alias);
      remove_persona (data->store, data->persona);
    }

  DEBUG ("Contact:'%s' no longer active", alias);
  persona_set_active (data->store, data->persona, FALSE, TRUE);

  persona_active_free (data);

  return FALSE;
}

static void
persona_updated_cb (FolksPersona *persona,
    GParamSpec *pspec,
    EmpathyPersonaStore *self)
{
  DEBUG ("Contact:'%s' updated, checking roster is in sync...",
      folks_alias_details_get_alias (FOLKS_ALIAS_DETAILS (persona)));

  update_persona (self, persona);
}

static void
add_persona_and_connect (EmpathyPersonaStore *self,
    FolksPersona *persona)
{
  /* We don't want any non-Telepathy personas */
  if (!TPF_IS_PERSONA (persona))
    return;

  g_signal_connect (persona, "notify::presence",
      (GCallback) persona_updated_cb, self);
  g_signal_connect (persona, "notify::presence-message",
      (GCallback) persona_updated_cb, self);
  g_signal_connect (persona, "notify::alias",
      (GCallback) persona_updated_cb, self);
  g_signal_connect (persona, "notify::avatar",
      (GCallback) persona_updated_cb, self);

  add_persona (self, persona);
}

static void
remove_persona_and_disconnect (EmpathyPersonaStore *self,
    FolksPersona *persona)
{
  if (!TPF_IS_PERSONA (persona))
    return;

  g_signal_handlers_disconnect_by_func (persona,
      (GCallback) persona_updated_cb, self);

  remove_persona (self, persona);
}

static void
add_persona (EmpathyPersonaStore *self,
    FolksPersona *persona)
{
  EmpathyPersonaStorePriv *priv;
  GtkTreeIter iter;
  GtkTreePath *path;
  FolksPersonaStore *store;
  TpContact *tp_contact;
  EmpathyContact *contact;
  const gchar *alias;

  if (!TPF_IS_PERSONA (persona))
    return;

  priv = GET_PRIV (self);

  alias = folks_alias_details_get_alias (FOLKS_ALIAS_DETAILS (persona));
  if (EMP_STR_EMPTY (alias))
    return;

  tp_contact = tpf_persona_get_contact (TPF_PERSONA (persona));
  if (tp_contact == NULL)
    return;

  contact = empathy_contact_dup_from_tp_contact (tp_contact);
  store = folks_persona_get_store (persona);

  gtk_list_store_insert_with_values (GTK_LIST_STORE (self), &iter, 0,
      EMPATHY_PERSONA_STORE_COL_NAME, alias,
      EMPATHY_PERSONA_STORE_COL_ACCOUNT_NAME,
          folks_persona_store_get_display_name (store),
      EMPATHY_PERSONA_STORE_COL_DISPLAY_ID,
          folks_persona_get_display_id (persona),
      EMPATHY_PERSONA_STORE_COL_PERSONA, persona,
      EMPATHY_PERSONA_STORE_COL_CAN_AUDIO_CALL,
          empathy_contact_get_capabilities (contact) &
              EMPATHY_CAPABILITIES_AUDIO,
      EMPATHY_PERSONA_STORE_COL_CAN_VIDEO_CALL,
          empathy_contact_get_capabilities (contact) &
              EMPATHY_CAPABILITIES_VIDEO,
      -1);

  g_object_unref (contact);

  path = gtk_tree_model_get_path (GTK_TREE_MODEL (self), &iter);
  g_hash_table_replace (priv->personas, g_object_ref (persona),
      gtk_tree_row_reference_new (GTK_TREE_MODEL (self), path));
  gtk_tree_path_free (path);

  update_persona (self, persona);
}

static void
remove_persona (EmpathyPersonaStore *self,
    FolksPersona *persona)
{
  EmpathyPersonaStorePriv *priv;
  GtkTreePath *path;
  GtkTreeIter iter;

  if (!TPF_IS_PERSONA (persona))
    return;

  priv = GET_PRIV (self);

  path = find_persona (self, persona);
  if (path == NULL)
    return;

  g_hash_table_remove (priv->personas, persona);

  gtk_tree_model_get_iter (GTK_TREE_MODEL (self), &iter, path);
  gtk_list_store_remove (GTK_LIST_STORE (self), &iter);
  gtk_tree_path_free (path);
}

static GdkPixbuf *
get_persona_status_icon (EmpathyPersonaStore *self,
    FolksPersona *persona)
{
  EmpathyPersonaStorePriv *priv = GET_PRIV (self);
  TpContact *tp_contact;
  EmpathyContact *contact;
  const gchar *protocol_name = NULL;
  gchar *icon_name = NULL;
  GdkPixbuf *pixbuf_status = NULL;
  const gchar *status_icon_name = NULL;

  tp_contact = tpf_persona_get_contact (TPF_PERSONA (persona));
  if (tp_contact == NULL)
    return NULL;

  contact = empathy_contact_dup_from_tp_contact (tp_contact);

  status_icon_name = empathy_icon_name_for_contact (contact);
  if (status_icon_name == NULL)
    {
      g_object_unref (contact);
      return NULL;
    }

  if (priv->show_protocols)
    {
      protocol_name = empathy_protocol_name_for_contact (contact);
      icon_name = g_strdup_printf ("%s-%s", status_icon_name, protocol_name);
    }
  else
    {
      icon_name = g_strdup_printf ("%s", status_icon_name);
    }

  pixbuf_status = g_hash_table_lookup (priv->status_icons, icon_name);

  if (pixbuf_status == NULL)
    {
      pixbuf_status = empathy_pixbuf_contact_status_icon_with_icon_name (
          contact, status_icon_name, priv->show_protocols);

      if (pixbuf_status != NULL)
        {
          g_hash_table_insert (priv->status_icons, g_strdup (icon_name),
              pixbuf_status);
        }
    }

  g_object_unref (contact);
  g_free (icon_name);

  return pixbuf_status;
}

static void
update_persona (EmpathyPersonaStore *self,
    FolksPersona *persona)
{
  EmpathyPersonaStorePriv *priv = GET_PRIV (self);
  GtkTreePath *path;
  gboolean do_set_active = FALSE;
  gboolean do_set_refresh = FALSE;
  const gchar *alias;

  path = find_persona (self, persona);
  alias = folks_alias_details_get_alias (FOLKS_ALIAS_DETAILS (persona));

  if (path == NULL)
    {
      DEBUG ("Contact:'%s' in list:NO, should be:YES", alias);

      add_persona (self, persona);

      if (priv->show_active)
        {
          do_set_active = TRUE;
          DEBUG ("Set active (contact added)");
        }
    }
  else
    {
      FolksPersonaStore *store;
      TpContact *tp_contact;
      EmpathyContact *contact;
      GtkTreeIter iter;
      GdkPixbuf *pixbuf_avatar = NULL;
      GdkPixbuf *pixbuf_status;
      gboolean now_online = FALSE;
      gboolean was_online = TRUE;

      DEBUG ("Contact:'%s' in list:YES, should be:YES", alias);

      gtk_tree_model_get_iter (GTK_TREE_MODEL (self), &iter, path);
      gtk_tree_path_free (path);

      /* Get online state now. */
      now_online = folks_presence_details_is_online (
          FOLKS_PRESENCE_DETAILS (persona));

      /* Get online state before. */
      gtk_tree_model_get (GTK_TREE_MODEL (self), &iter,
          EMPATHY_PERSONA_STORE_COL_IS_ONLINE, &was_online,
          -1);

      /* Is this really an update or an online/offline. */
      if (priv->show_active)
        {
          if (was_online != now_online)
            {
              do_set_active = TRUE;
              do_set_refresh = TRUE;

              DEBUG ("Set active (contact updated %s)",
                  was_online ? "online  -> offline" : "offline -> online");
            }
          else
            {
              /* Was TRUE for presence updates. */
              /* do_set_active = FALSE;  */
              do_set_refresh = TRUE;
              DEBUG ("Set active (contact updated)");
            }
        }

      /* We still need to use EmpathyContact for the capabilities stuff */
      tp_contact = tpf_persona_get_contact (TPF_PERSONA (persona));
      if (tp_contact != NULL)
        {
          contact = empathy_contact_dup_from_tp_contact (tp_contact);
          store = folks_persona_get_store (persona);

          pixbuf_avatar = empathy_pixbuf_avatar_from_contact_scaled (contact,
              32, 32);
          pixbuf_status = get_persona_status_icon (self, persona);

          gtk_list_store_set (GTK_LIST_STORE (self), &iter,
              EMPATHY_PERSONA_STORE_COL_ICON_STATUS, pixbuf_status,
              EMPATHY_PERSONA_STORE_COL_PIXBUF_AVATAR, pixbuf_avatar,
              EMPATHY_PERSONA_STORE_COL_PIXBUF_AVATAR_VISIBLE,
                  priv->show_avatars,
              EMPATHY_PERSONA_STORE_COL_NAME, alias,
              EMPATHY_PERSONA_STORE_COL_ACCOUNT_NAME,
                  folks_persona_store_get_display_name (store),
              EMPATHY_PERSONA_STORE_COL_DISPLAY_ID,
                  folks_persona_get_display_id (persona),
              EMPATHY_PERSONA_STORE_COL_PRESENCE_TYPE,
                  folks_presence_details_get_presence_type (
                      FOLKS_PRESENCE_DETAILS (persona)),
              EMPATHY_PERSONA_STORE_COL_STATUS,
                  folks_presence_details_get_presence_message (
                      FOLKS_PRESENCE_DETAILS (persona)),
              EMPATHY_PERSONA_STORE_COL_IS_ONLINE, now_online,
              EMPATHY_PERSONA_STORE_COL_CAN_AUDIO_CALL,
                  empathy_contact_get_capabilities (contact) &
                    EMPATHY_CAPABILITIES_AUDIO,
              EMPATHY_PERSONA_STORE_COL_CAN_VIDEO_CALL,
                  empathy_contact_get_capabilities (contact) &
                    EMPATHY_CAPABILITIES_VIDEO,
              -1);

          g_object_unref (contact);
        }

      if (pixbuf_avatar)
        g_object_unref (pixbuf_avatar);
    }

  if (priv->show_active && do_set_active)
    {
      persona_set_active (self, persona, do_set_active, do_set_refresh);

      if (do_set_active)
        {
          ShowActiveData *data;

          data = persona_active_new (self, persona, FALSE);
          data->timeout = g_timeout_add_seconds (ACTIVE_USER_SHOW_TIME,
              (GSourceFunc) persona_active_cb,
              data);
        }
    }

  /* FIXME: when someone goes online then offline quickly, the
   * first timeout sets the user to be inactive and the second
   * timeout removes the user from the contact list, really we
   * should remove the first timeout.
   */
}

static void
individual_personas_changed_cb (GObject *object,
    GeeSet *added,
    GeeSet *removed,
    EmpathyPersonaStore *self)
{
  GeeIterator *iter;

  /* One of the personas' row references might hold the last reference to the
   * PersonaStore, so we need to keep a reference ourselves so we don't get
   * finalised. */
  g_object_ref (self);

  /* Remove the old personas. */
  iter = gee_iterable_iterator (GEE_ITERABLE (removed));
  while (gee_iterator_next (iter))
    {
      FolksPersona *persona = gee_iterator_get (iter);
      remove_persona_and_disconnect (self, persona);
      g_clear_object (&persona);
    }
  g_clear_object (&iter);

  /* Add each of the new personas to the tree model */
  iter = gee_iterable_iterator (GEE_ITERABLE (added));
  while (gee_iterator_next (iter))
    {
      FolksPersona *persona = gee_iterator_get (iter);
      add_persona_and_connect (self, persona);
      g_clear_object (&persona);
    }
  g_clear_object (&iter);

  g_object_unref (self);
}

static gint
sort_personas (FolksPersona *persona_a,
    FolksPersona *persona_b)
{
  EmpathyContact *contact;
  TpAccount *account_a, *account_b;
  TpContact *tp_contact_a, *tp_contact_b;
  gint ret_val;

  g_return_val_if_fail (persona_a != NULL || persona_b != NULL, 0);

  /* alias */
  ret_val = g_utf8_collate (
      folks_alias_details_get_alias (FOLKS_ALIAS_DETAILS (persona_a)),
      folks_alias_details_get_alias (FOLKS_ALIAS_DETAILS (persona_b)));

  if (ret_val != 0)
    goto out;

  /* identifier */
  ret_val = g_utf8_collate (folks_persona_get_display_id (persona_a),
          folks_persona_get_display_id (persona_b));

  if (ret_val != 0)
    goto out;

  tp_contact_a = tpf_persona_get_contact (TPF_PERSONA (persona_a));
  tp_contact_b = tpf_persona_get_contact (TPF_PERSONA (persona_b));

  /* handle the case that one or more of these personas are from the cache */
  if (tp_contact_a == NULL || tp_contact_b == NULL)
    {
      ret_val = (tp_contact_a != NULL ? 1 : -1);
      goto out;
    }

  contact = empathy_contact_dup_from_tp_contact (tp_contact_a);
  account_a = empathy_contact_get_account (contact);
  g_object_unref (contact);

  contact = empathy_contact_dup_from_tp_contact (tp_contact_b);
  account_b = empathy_contact_get_account (contact);
  g_object_unref (contact);

  /* protocol */
  ret_val = strcmp (tp_account_get_protocol (account_a),
        tp_account_get_protocol (account_b));

  if (ret_val != 0)
    goto out;

  /* account ID */
  ret_val = strcmp (tp_proxy_get_object_path (account_a),
        tp_proxy_get_object_path (account_b));

out:
  return ret_val;
}

static gint
state_sort_func (GtkTreeModel *model,
    GtkTreeIter *iter_a,
    GtkTreeIter *iter_b,
    gpointer user_data)
{
  gint ret_val;
  gchar *name_a, *name_b;
  FolksPersona *persona_a, *persona_b;

  gtk_tree_model_get (model, iter_a,
          EMPATHY_PERSONA_STORE_COL_NAME, &name_a,
          EMPATHY_PERSONA_STORE_COL_PERSONA, &persona_a,
          -1);
  gtk_tree_model_get (model, iter_b,
          EMPATHY_PERSONA_STORE_COL_NAME, &name_b,
          EMPATHY_PERSONA_STORE_COL_PERSONA, &persona_b,
          -1);

  if (persona_a == NULL || persona_b == NULL) {
    ret_val = 0;
    goto free_and_out;
  }

  /* If we managed to get this far, we can start looking at
   * the presences.
   */
  ret_val = -tp_connection_presence_type_cmp_availability (
      folks_presence_details_get_presence_type (
          FOLKS_PRESENCE_DETAILS (persona_a)),
      folks_presence_details_get_presence_type (
          FOLKS_PRESENCE_DETAILS (persona_b)));

  if (ret_val == 0) {
    /* Fallback: compare by name et al. */
    ret_val = sort_personas (persona_a, persona_b);
  }

free_and_out:
  g_free (name_a);
  g_free (name_b);

  tp_clear_object (&persona_a);
  tp_clear_object (&persona_b);

  return ret_val;
}

static gint
name_sort_func (GtkTreeModel *model,
    GtkTreeIter *iter_a,
    GtkTreeIter *iter_b,
    gpointer user_data)
{
  gchar *name_a, *name_b;
  FolksPersona *persona_a, *persona_b;
  gint ret_val;

  gtk_tree_model_get (model, iter_a,
          EMPATHY_PERSONA_STORE_COL_NAME, &name_a,
          EMPATHY_PERSONA_STORE_COL_PERSONA, &persona_a,
          -1);
  gtk_tree_model_get (model, iter_b,
          EMPATHY_PERSONA_STORE_COL_NAME, &name_b,
          EMPATHY_PERSONA_STORE_COL_PERSONA, &persona_b,
          -1);

  if (persona_a == NULL || persona_b == NULL)
    ret_val = 0;
  else
    ret_val = sort_personas (persona_a, persona_b);

  tp_clear_object (&persona_a);
  tp_clear_object (&persona_b);

  return ret_val;
}

static GtkTreePath *
find_persona (EmpathyPersonaStore *self,
    FolksPersona *persona)
{
  EmpathyPersonaStorePriv *priv = GET_PRIV (self);
  GtkTreeRowReference *row;

  row = g_hash_table_lookup (priv->personas, persona);
  if (row == NULL)
    return NULL;

  return gtk_tree_row_reference_get_path (row);
}

static gboolean
update_list_mode_foreach (GtkTreeModel *model,
    GtkTreePath *path,
    GtkTreeIter *iter,
    EmpathyPersonaStore *self)
{
  EmpathyPersonaStorePriv *priv;
  FolksPersona *persona;
  GdkPixbuf *pixbuf_status;

  priv = GET_PRIV (self);

  gtk_tree_model_get (model, iter,
      EMPATHY_PERSONA_STORE_COL_PERSONA, &persona,
      -1);

  if (persona == NULL)
    return FALSE;

  /* get icon from hash_table */
  pixbuf_status = get_persona_status_icon (self, persona);

  gtk_list_store_set (GTK_LIST_STORE (self), iter,
      EMPATHY_PERSONA_STORE_COL_ICON_STATUS, pixbuf_status,
      EMPATHY_PERSONA_STORE_COL_PIXBUF_AVATAR_VISIBLE, priv->show_avatars,
      -1);

  tp_clear_object (&persona);

  return FALSE;
}

static void
set_up (EmpathyPersonaStore *self)
{
  EmpathyPersonaStorePriv *priv;
  GType types[] = {
    GDK_TYPE_PIXBUF,      /* Status pixbuf */
    GDK_TYPE_PIXBUF,      /* Avatar pixbuf */
    G_TYPE_BOOLEAN,       /* Avatar pixbuf visible */
    G_TYPE_STRING,        /* Name */
    G_TYPE_STRING,        /* Account name */
    G_TYPE_STRING,        /* Display ID */
    G_TYPE_UINT,          /* Presence type */
    G_TYPE_STRING,        /* Status string */
    FOLKS_TYPE_PERSONA,   /* Persona */
    G_TYPE_BOOLEAN,       /* Is active */
    G_TYPE_BOOLEAN,       /* Is online */
    G_TYPE_BOOLEAN,       /* Can make audio calls */
    G_TYPE_BOOLEAN,       /* Can make video calls */
  };

  priv = GET_PRIV (self);

  gtk_list_store_set_column_types (GTK_LIST_STORE (self),
      EMPATHY_PERSONA_STORE_COL_COUNT, types);

  /* Set up sorting */
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (self),
      EMPATHY_PERSONA_STORE_COL_NAME, name_sort_func, self, NULL);
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (self),
      EMPATHY_PERSONA_STORE_COL_STATUS, state_sort_func, self, NULL);

  priv->sort_criterion = EMPATHY_PERSONA_STORE_SORT_NAME;
  empathy_persona_store_set_sort_criterion (self, priv->sort_criterion);
}

static void
empathy_persona_store_init (EmpathyPersonaStore *self)
{
  EmpathyPersonaStorePriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_PERSONA_STORE, EmpathyPersonaStorePriv);

  self->priv = priv;

  priv->show_avatars = TRUE;
  priv->show_protocols = FALSE;
  priv->inhibit_active = g_timeout_add_seconds (ACTIVE_USER_WAIT_TO_ENABLE_TIME,
      (GSourceFunc) inhibit_active_cb, self);

  priv->status_icons = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      g_object_unref);
  priv->personas = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      g_object_unref, (GDestroyNotify) gtk_tree_row_reference_free);

  set_up (self);
}

static void
get_property (GObject *object,
    guint param_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyPersonaStorePriv *priv = GET_PRIV (object);

  switch (param_id)
    {
      case PROP_INDIVIDUAL:
        g_value_set_object (value, priv->individual);
        break;
      case PROP_SHOW_AVATARS:
        g_value_set_boolean (value, priv->show_avatars);
        break;
      case PROP_SHOW_PROTOCOLS:
        g_value_set_boolean (value, priv->show_protocols);
        break;
      case PROP_SORT_CRITERION:
        g_value_set_enum (value, priv->sort_criterion);
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
  EmpathyPersonaStore *self = EMPATHY_PERSONA_STORE (object);

  switch (param_id)
    {
      case PROP_INDIVIDUAL:
        empathy_persona_store_set_individual (self, g_value_get_object (value));
        break;
      case PROP_SHOW_AVATARS:
        empathy_persona_store_set_show_avatars (self,
            g_value_get_boolean (value));
        break;
      case PROP_SHOW_PROTOCOLS:
        empathy_persona_store_set_show_protocols (self,
            g_value_get_boolean (value));
        break;
      case PROP_SORT_CRITERION:
        empathy_persona_store_set_sort_criterion (self,
            g_value_get_enum (value));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    }
}

static void
dispose (GObject *object)
{
  EmpathyPersonaStorePriv *priv = GET_PRIV (object);

  empathy_persona_store_set_individual (EMPATHY_PERSONA_STORE (object), NULL);

  if (priv->inhibit_active != 0)
    {
      g_source_remove (priv->inhibit_active);
      priv->inhibit_active = 0;
    }

  if (priv->setup_idle_id != 0)
    {
      g_source_remove (priv->setup_idle_id);
      priv->setup_idle_id = 0;
    }

  G_OBJECT_CLASS (empathy_persona_store_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
  EmpathyPersonaStorePriv *priv = GET_PRIV (object);

  g_hash_table_unref (priv->status_icons);
  g_hash_table_unref (priv->personas);

  G_OBJECT_CLASS (empathy_persona_store_parent_class)->finalize (object);
}

static void
empathy_persona_store_class_init (EmpathyPersonaStoreClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = get_property;
  object_class->set_property = set_property;
  object_class->dispose = dispose;
  object_class->finalize = finalize;

  /**
   * EmpathyPersonaStore:individual:
   *
   * The #FolksIndividual whose personas should be listed by the store. This
   * may be %NULL, which results in an empty store.
   */
  g_object_class_install_property (object_class, PROP_INDIVIDUAL,
      g_param_spec_object ("individual",
          "Individual",
          "The FolksIndividual whose Personas should be listed by the store.",
          FOLKS_TYPE_INDIVIDUAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * EmpathyPersonaStore:show-avatars:
   *
   * Whether the store should display avatars for personas. This is a property
   * of the store rather than of #EmpathyPersonaView for efficiency reasons.
   */
  g_object_class_install_property (object_class, PROP_SHOW_AVATARS,
      g_param_spec_boolean ("show-avatars",
          "Show Avatars",
          "Whether the store should display avatars for personas.",
          TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * EmpathyPersonaStore:show-protocols:
   *
   * Whether the store should display protocol icons for personas. This is a
   * property of the store rather than of #EmpathyPersonaView because it is
   * closely tied in with #EmpathyPersonaStore:show-avatars.
   */
  g_object_class_install_property (object_class, PROP_SHOW_PROTOCOLS,
      g_param_spec_boolean ("show-protocols",
          "Show Protocols",
          "Whether the store should display protocol icons for personas.",
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * EmpathyPersonaStore:sort-criterion:
   *
   * The criterion used to sort the personas in the store.
   */
  g_object_class_install_property (object_class, PROP_SORT_CRITERION,
      g_param_spec_enum ("sort-criterion",
          "Sort criterion",
          "The sort criterion to use for sorting the persona list",
          EMPATHY_TYPE_PERSONA_STORE_SORT,
          EMPATHY_PERSONA_STORE_SORT_NAME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (object_class, sizeof (EmpathyPersonaStorePriv));
}

/**
 * empathy_persona_store_new:
 * @individual: the #FolksIndividual whose personas should be used in the store,
 * or %NULL
 *
 * Create a new #EmpathyPersonaStore with the personas from the given
 * @individual.
 *
 * Return value: a new #EmpathyPersonaStore
 */
EmpathyPersonaStore *
empathy_persona_store_new (FolksIndividual *individual)
{
  g_return_val_if_fail (individual == NULL || FOLKS_IS_INDIVIDUAL (individual),
      NULL);

  return g_object_new (EMPATHY_TYPE_PERSONA_STORE,
      "individual", individual, NULL);
}

/**
 * empathy_persona_store_get_individual:
 * @self: an #EmpathyPersonaStore
 *
 * Get the value of #EmpathyPersonaStore:individual.
 *
 * Return value: the individual being displayed by the store, or %NULL
 */
FolksIndividual *
empathy_persona_store_get_individual (EmpathyPersonaStore *self)
{
  g_return_val_if_fail (EMPATHY_IS_PERSONA_STORE (self), NULL);

  return GET_PRIV (self)->individual;
}

/**
 * empathy_persona_store_set_individual:
 * @self: an #EmpathyPersonaStore
 * @individual: the new individual to display in the store, or %NULL
 *
 * Set #EmpathyPersonaStore:individual to @individual, replacing the personas
 * which were in the store with the personas belonging to @individual, or with
 * nothing if @individual is %NULL.
 */
void
empathy_persona_store_set_individual (EmpathyPersonaStore *self,
    FolksIndividual *individual)
{
  EmpathyPersonaStorePriv *priv;

  g_return_if_fail (EMPATHY_IS_PERSONA_STORE (self));
  g_return_if_fail (individual == NULL || FOLKS_IS_INDIVIDUAL (individual));

  priv = GET_PRIV (self);

  /* Remove the old individual */
  if (priv->individual != NULL)
    {
      GeeSet *personas;
      GeeIterator *iter;

      g_signal_handlers_disconnect_by_func (priv->individual,
          (GCallback) individual_personas_changed_cb, self);

      /* Disconnect from and remove all personas belonging to this individual */
      personas = folks_individual_get_personas (priv->individual);
      iter = gee_iterable_iterator (GEE_ITERABLE (personas));
      while (gee_iterator_next (iter))
        {
          FolksPersona *persona = gee_iterator_get (iter);
          remove_persona_and_disconnect (self, persona);
          g_clear_object (&persona);
        }
      g_clear_object (&iter);

      g_object_unref (priv->individual);
    }

  priv->individual = individual;

  /* Add the new individual */
  if (individual != NULL)
    {
      GeeSet *personas;
      GeeIterator *iter;

      g_object_ref (individual);

      g_signal_connect (individual, "personas-changed",
          (GCallback) individual_personas_changed_cb, self);

      /* Add pre-existing Personas */

      personas = folks_individual_get_personas (individual);
      iter = gee_iterable_iterator (GEE_ITERABLE (personas));
      while (gee_iterator_next (iter))
        {
          FolksPersona *persona = gee_iterator_get (iter);
          add_persona_and_connect (self, persona);
          g_clear_object (&persona);
        }
      g_clear_object (&iter);
    }

  g_object_notify (G_OBJECT (self), "individual");
}

/**
 * empathy_persona_store_get_show_avatars:
 * @self: an #EmpathyPersonaStore
 *
 * Get the value of #EmpathyPersonaStore:show-avatars.
 *
 * Return value: %TRUE if avatars are made available by the store, %FALSE
 * otherwise
 */
gboolean
empathy_persona_store_get_show_avatars (EmpathyPersonaStore *self)
{
  g_return_val_if_fail (EMPATHY_IS_PERSONA_STORE (self), TRUE);

  return GET_PRIV (self)->show_avatars;
}

/**
 * empathy_persona_store_set_show_avatars:
 * @self: an #EmpathyPersonaStore
 * @show_avatars: %TRUE to make avatars available through the store, %FALSE
 * otherwise
 *
 * Set #EmpathyPersonaStore:show-avatars to @show_avatars.
 */
void
empathy_persona_store_set_show_avatars (EmpathyPersonaStore *self,
    gboolean show_avatars)
{
  EmpathyPersonaStorePriv *priv;

  g_return_if_fail (EMPATHY_IS_PERSONA_STORE (self));

  priv = GET_PRIV (self);
  priv->show_avatars = show_avatars;

  gtk_tree_model_foreach (GTK_TREE_MODEL (self),
      (GtkTreeModelForeachFunc) update_list_mode_foreach, self);

  g_object_notify (G_OBJECT (self), "show-avatars");
}

/**
 * empathy_persona_store_get_show_protocols:
 * @self: an #EmpathyPersonaStore
 *
 * Get the value of #EmpathyPersonaStore:show-protocols.
 *
 * Return value: %TRUE if protocol images are made available by the store,
 * %FALSE otherwise
 */
gboolean
empathy_persona_store_get_show_protocols (EmpathyPersonaStore *self)
{
  g_return_val_if_fail (EMPATHY_IS_PERSONA_STORE (self), TRUE);

  return GET_PRIV (self)->show_protocols;
}

/**
 * empathy_persona_store_set_show_protocols:
 * @self: an #EmpathyPersonaStore
 * @show_protocols: %TRUE to make protocol images available through the store,
 * %FALSE otherwise
 *
 * Set #EmpathyPersonaStore:show-protocols to @show_protocols.
 */
void
empathy_persona_store_set_show_protocols (EmpathyPersonaStore *self,
    gboolean show_protocols)
{
  EmpathyPersonaStorePriv *priv;

  g_return_if_fail (EMPATHY_IS_PERSONA_STORE (self));

  priv = GET_PRIV (self);
  priv->show_protocols = show_protocols;

  gtk_tree_model_foreach (GTK_TREE_MODEL (self),
      (GtkTreeModelForeachFunc) update_list_mode_foreach, self);

  g_object_notify (G_OBJECT (self), "show-protocols");
}

/**
 * empathy_persona_store_get_sort_criterion:
 * @self: an #EmpathyPersonaStore
 *
 * Get the value of #EmpathyPersonaStore:sort-criterion.
 *
 * Return value: the criterion used to sort the personas in the store
 */
EmpathyPersonaStoreSort
empathy_persona_store_get_sort_criterion (EmpathyPersonaStore *self)
{
  g_return_val_if_fail (EMPATHY_IS_PERSONA_STORE (self), 0);

  return GET_PRIV (self)->sort_criterion;
}

/**
 * empathy_persona_store_set_sort_criterion:
 * @self: an #EmpathyPersonaStore
 * @show_avatars: a criterion to be used to sort personas in the store
 *
 * Set #EmpathyPersonaStore:sort-criterion to @sort_criterion.
 */
void
empathy_persona_store_set_sort_criterion (EmpathyPersonaStore *self,
    EmpathyPersonaStoreSort sort_criterion)
{
  EmpathyPersonaStorePriv *priv;

  g_return_if_fail (EMPATHY_IS_PERSONA_STORE (self));

  priv = GET_PRIV (self);
  priv->sort_criterion = sort_criterion;

  switch (sort_criterion)
    {
      case EMPATHY_PERSONA_STORE_SORT_STATE:
        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (self),
            EMPATHY_PERSONA_STORE_COL_STATUS, GTK_SORT_ASCENDING);
        break;
      case EMPATHY_PERSONA_STORE_SORT_NAME:
        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (self),
            EMPATHY_PERSONA_STORE_COL_NAME, GTK_SORT_ASCENDING);
        break;
      default:
        g_assert_not_reached ();
        break;
    }

  g_object_notify (G_OBJECT (self), "sort-criterion");
}
