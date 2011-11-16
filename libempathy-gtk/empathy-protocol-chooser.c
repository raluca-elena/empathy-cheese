/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/*
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
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Jonny Lamb <jonny.lamb@collabora.co.uk>
 */

#include <config.h>

#include <string.h>

#include <telepathy-glib/util.h>

#include <gtk/gtk.h>

#include <glib/gi18n-lib.h>

#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-connection-managers.h>

#include "empathy-protocol-chooser.h"
#include "empathy-ui-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

/**
 * SECTION:empathy-protocol-chooser
 * @title: EmpathyProtocolChooser
 * @short_description: A widget used to choose from a list of protocols
 * @include: libempathy-gtk/empathy-protocol-chooser.h
 *
 * #EmpathyProtocolChooser is a widget which extends #GtkComboBox to provides a
 * chooser of available protocols.
 */

/**
 * EmpathyProtocolChooser:
 * @parent: parent object
 *
 * Widget which extends #GtkComboBox to provide a chooser of available
 * protocols.
 */

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyProtocolChooser)
typedef struct
{
  GtkListStore *store;

  gboolean dispose_run;
  EmpathyConnectionManagers *cms;

  EmpathyProtocolChooserFilterFunc filter_func;
  gpointer filter_user_data;

  GHashTable *protocols;
} EmpathyProtocolChooserPriv;

enum
{
  COL_ICON,
  COL_LABEL,
  COL_CM,
  COL_PROTOCOL_NAME,
  COL_SERVICE,
  COL_COUNT
};

G_DEFINE_TYPE (EmpathyProtocolChooser, empathy_protocol_chooser,
    GTK_TYPE_COMBO_BOX);

static gint
protocol_chooser_sort_protocol_value (const gchar *protocol_name)
{
  guint i;
  const gchar *names[] = {
    "jabber",
    "local-xmpp",
    "gtalk",
    NULL
  };

  for (i = 0 ; names[i]; i++)
    {
      if (strcmp (protocol_name, names[i]) == 0)
        return i;
    }

  return i;
}

static gint
protocol_chooser_sort_func (GtkTreeModel *model,
    GtkTreeIter  *iter_a,
    GtkTreeIter  *iter_b,
    gpointer      user_data)
{
  gchar *protocol_a;
  gchar *protocol_b;
  gint cmp = 0;

  gtk_tree_model_get (model, iter_a,
      COL_PROTOCOL_NAME, &protocol_a,
      -1);
  gtk_tree_model_get (model, iter_b,
      COL_PROTOCOL_NAME, &protocol_b,
      -1);

  cmp = protocol_chooser_sort_protocol_value (protocol_a);
  cmp -= protocol_chooser_sort_protocol_value (protocol_b);
  if (cmp == 0)
    {
      cmp = strcmp (protocol_a, protocol_b);
      /* only happens for jabber where there is one entry for gtalk and one for
       * non-gtalk */
      if (cmp == 0)
        {
          gchar *service;

          gtk_tree_model_get (model, iter_a,
            COL_SERVICE, &service,
            -1);

          if (service != NULL)
            cmp = 1;
          else
            cmp = -1;

          g_free (service);
        }
    }

  g_free (protocol_a);
  g_free (protocol_b);
  return cmp;
}

static void
protocol_choosers_add_cm (EmpathyProtocolChooser *chooser,
    TpConnectionManager *cm)
{
  EmpathyProtocolChooserPriv *priv = GET_PRIV (chooser);
  const TpConnectionManagerProtocol * const *iter;

  for (iter = cm->protocols; iter != NULL && *iter != NULL; iter++)
    {
      const TpConnectionManagerProtocol *proto = *iter;
      gchar *icon_name;
      const gchar *display_name;
      const gchar *saved_cm_name;

      saved_cm_name = g_hash_table_lookup (priv->protocols, proto->name);

      if (!tp_strdiff (cm->name, "haze") && saved_cm_name != NULL &&
          tp_strdiff (saved_cm_name, "haze"))
        /* the CM we're adding is a haze implementation of something we already
         * have; drop it.
         */
        continue;

      if (!tp_strdiff (cm->name, "haze") &&
          !tp_strdiff (proto->name, "facebook"))
        /* Facebook now supports XMPP so drop the purple facebook plugin; user
         * should use Gabble */
        continue;

      if (!tp_strdiff (cm->name, "haze") &&
          !tp_strdiff (proto->name, "sip"))
        /* Haze's SIP implementation is pretty useless (bgo #629736) */
        continue;

      if (tp_strdiff (cm->name, "haze") && !tp_strdiff (saved_cm_name, "haze"))
        {
          GtkTreeIter titer;
          gboolean valid;
          TpConnectionManager *haze_cm;

          /* let's this CM replace the haze implementation */
          valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (priv->store),
              &titer);

          while (valid)
            {
              gchar *haze_proto_name = NULL;

              gtk_tree_model_get (GTK_TREE_MODEL (priv->store), &titer,
                  COL_PROTOCOL_NAME, &haze_proto_name,
                  COL_CM, &haze_cm, -1);

              if (haze_cm == NULL)
                continue;

              if (!tp_strdiff (haze_cm->name, "haze") &&
                  !tp_strdiff (haze_proto_name, proto->name))
                {
                  gtk_list_store_remove (priv->store, &titer);
                  g_object_unref (haze_cm);
                  g_free (haze_proto_name);
                  break;
                }

              g_object_unref (haze_cm);
              g_free (haze_proto_name);
              valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (priv->store),
                  &titer);
            }
        }

      g_hash_table_insert (priv->protocols,
          g_strdup (proto->name), g_strdup (cm->name));

      icon_name = empathy_protocol_icon_name (proto->name);

      display_name = empathy_protocol_name_to_display_name (proto->name);

      gtk_list_store_insert_with_values (priv->store,
          NULL, 0,
          COL_ICON, icon_name,
          COL_LABEL, display_name,
          COL_CM, cm,
          COL_PROTOCOL_NAME, proto->name,
          -1);

      if (!tp_strdiff (proto->name, "jabber") &&
          !tp_strdiff (cm->name, "gabble"))
        {
          display_name = empathy_service_name_to_display_name ("google-talk");
          gtk_list_store_insert_with_values (priv->store,
             NULL, 0,
             COL_ICON, "im-google-talk",
             COL_LABEL, display_name,
             COL_CM, cm,
             COL_PROTOCOL_NAME, proto->name,
             COL_SERVICE, "google-talk",
             -1);

          display_name = empathy_service_name_to_display_name ("facebook");
          gtk_list_store_insert_with_values (priv->store,
             NULL, 0,
             COL_ICON, "im-facebook",
             COL_LABEL, display_name,
             COL_CM, cm,
             COL_PROTOCOL_NAME, proto->name,
             COL_SERVICE, "facebook",
             -1);
        }

      g_free (icon_name);
    }
}

static void
protocol_chooser_add_cms_list (EmpathyProtocolChooser *protocol_chooser,
    GList *cms)
{
  GList *l;

  for (l = cms; l != NULL; l = l->next)
    protocol_choosers_add_cm (protocol_chooser, l->data);

  gtk_combo_box_set_active (GTK_COMBO_BOX (protocol_chooser), 0);
}

static void
protocol_chooser_cms_prepare_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyConnectionManagers *cms = EMPATHY_CONNECTION_MANAGERS (source);
  EmpathyProtocolChooser *protocol_chooser = user_data;

  if (!empathy_connection_managers_prepare_finish (cms, result, NULL))
    return;

  protocol_chooser_add_cms_list (protocol_chooser,
      empathy_connection_managers_get_cms (cms));
}

static void
protocol_chooser_constructed (GObject *object)
{
  EmpathyProtocolChooser *protocol_chooser;
  EmpathyProtocolChooserPriv *priv;
  GtkCellRenderer *renderer;

  priv = GET_PRIV (object);
  protocol_chooser = EMPATHY_PROTOCOL_CHOOSER (object);

  /* set up combo box with new store */
  priv->store = gtk_list_store_new (COL_COUNT,
          G_TYPE_STRING,    /* Icon name */
          G_TYPE_STRING,    /* Label     */
          G_TYPE_OBJECT,    /* CM */
          G_TYPE_STRING,    /* protocol name  */
          G_TYPE_STRING);   /* service */

  /* Set the protocol sort function */
  gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (priv->store),
      COL_PROTOCOL_NAME,
      protocol_chooser_sort_func,
      NULL, NULL);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->store),
      COL_PROTOCOL_NAME,
      GTK_SORT_ASCENDING);

  gtk_combo_box_set_model (GTK_COMBO_BOX (object),
      GTK_TREE_MODEL (priv->store));

  renderer = gtk_cell_renderer_pixbuf_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (object), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (object), renderer,
      "icon-name", COL_ICON,
      NULL);
  g_object_set (renderer, "stock-size", GTK_ICON_SIZE_BUTTON, NULL);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (object), renderer, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (object), renderer,
      "text", COL_LABEL,
      NULL);

  empathy_connection_managers_prepare_async (priv->cms,
      protocol_chooser_cms_prepare_cb, protocol_chooser);

  if (G_OBJECT_CLASS (empathy_protocol_chooser_parent_class)->constructed)
    G_OBJECT_CLASS
      (empathy_protocol_chooser_parent_class)->constructed (object);
}

static void
empathy_protocol_chooser_init (EmpathyProtocolChooser *protocol_chooser)
{
  EmpathyProtocolChooserPriv *priv =
    G_TYPE_INSTANCE_GET_PRIVATE (protocol_chooser,
        EMPATHY_TYPE_PROTOCOL_CHOOSER, EmpathyProtocolChooserPriv);

  priv->dispose_run = FALSE;
  priv->cms = empathy_connection_managers_dup_singleton ();
  priv->protocols = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_free);

  protocol_chooser->priv = priv;
}

static void
protocol_chooser_finalize (GObject *object)
{
  EmpathyProtocolChooser *protocol_chooser = EMPATHY_PROTOCOL_CHOOSER (object);
  EmpathyProtocolChooserPriv *priv = GET_PRIV (protocol_chooser);

  if (priv->protocols)
    {
      g_hash_table_unref (priv->protocols);
      priv->protocols = NULL;
    }

  (G_OBJECT_CLASS (empathy_protocol_chooser_parent_class)->finalize) (object);
}

static void
protocol_chooser_dispose (GObject *object)
{
  EmpathyProtocolChooser *protocol_chooser = EMPATHY_PROTOCOL_CHOOSER (object);
  EmpathyProtocolChooserPriv *priv = GET_PRIV (protocol_chooser);

  if (priv->dispose_run)
    return;

  priv->dispose_run = TRUE;

  if (priv->store)
    {
      g_object_unref (priv->store);
      priv->store = NULL;
    }

  if (priv->cms)
    {
      g_object_unref (priv->cms);
      priv->cms = NULL;
    }

  (G_OBJECT_CLASS (empathy_protocol_chooser_parent_class)->dispose) (object);
}

static void
empathy_protocol_chooser_class_init (EmpathyProtocolChooserClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = protocol_chooser_constructed;
  object_class->dispose = protocol_chooser_dispose;
  object_class->finalize = protocol_chooser_finalize;

  g_type_class_add_private (object_class, sizeof (EmpathyProtocolChooserPriv));
}

static gboolean
protocol_chooser_filter_visible_func (GtkTreeModel *model,
    GtkTreeIter *iter,
    gpointer user_data)
{
  EmpathyProtocolChooser *protocol_chooser = user_data;
  EmpathyProtocolChooserPriv *priv = GET_PRIV (protocol_chooser);
  TpConnectionManager *cm = NULL;
  gchar *protocol_name = NULL;
  gboolean visible = FALSE;
  gchar *service;

  gtk_tree_model_get (model, iter,
      COL_CM, &cm,
      COL_PROTOCOL_NAME, &protocol_name,
      COL_SERVICE, &service,
      -1);

  if (cm != NULL && protocol_name != NULL)
    {
      TpConnectionManagerProtocol *protocol;

      protocol = (TpConnectionManagerProtocol *)
        tp_connection_manager_get_protocol (cm, protocol_name);

      if (protocol != NULL)
        {
          visible = priv->filter_func (cm, protocol, service,
              priv->filter_user_data);
        }
    }

  if (cm != NULL)
    g_object_unref (cm);

  g_free (service);
  return visible;
}

/* public methods */

/**
 * empathy_protocol_chooser_get_selected_protocol:
 * @protocol_chooser: an #EmpathyProtocolChooser
 *
 * Returns a pointer to the selected #TpConnectionManagerProtocol in
 * @protocol_chooser.
 *
 * Return value: a pointer to the selected #TpConnectionManagerProtocol
 */
TpConnectionManager *
empathy_protocol_chooser_dup_selected (
    EmpathyProtocolChooser *protocol_chooser,
    TpConnectionManagerProtocol **protocol,
    gchar **service)
{
  GtkTreeIter iter;
  TpConnectionManager *cm = NULL;
  GtkTreeModel *cur_model;

  g_return_val_if_fail (EMPATHY_IS_PROTOCOL_CHOOSER (protocol_chooser), NULL);

  /* get the current model from the chooser, as we could either be filtering
   * or not.
   */
  cur_model = gtk_combo_box_get_model (GTK_COMBO_BOX (protocol_chooser));

  if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (protocol_chooser), &iter))
    {
      gtk_tree_model_get (GTK_TREE_MODEL (cur_model), &iter,
          COL_CM, &cm,
          -1);

      if (protocol != NULL)
        {
          gchar *protocol_name = NULL;

          gtk_tree_model_get (GTK_TREE_MODEL (cur_model), &iter,
              COL_PROTOCOL_NAME, &protocol_name,
              -1);

          *protocol = (TpConnectionManagerProtocol *)
            tp_connection_manager_get_protocol (cm, protocol_name);

          g_free (protocol_name);

          if (*protocol == NULL)
            {
              /* For some reason the CM doesn't know about this protocol
               * any more */
              g_object_unref (cm);
              return NULL;
            }
        }

      if (service != NULL)
        {
          gtk_tree_model_get (GTK_TREE_MODEL (cur_model), &iter,
              COL_SERVICE, service,
              -1);
        }
    }

  return cm;
}

/**
 * empathy_protocol_chooser_new:
 *
 * Triggers the creation of a new #EmpathyProtocolChooser.
 *
 * Return value: a new #EmpathyProtocolChooser widget
 */

GtkWidget *
empathy_protocol_chooser_new (void)
{
  return GTK_WIDGET (g_object_new (EMPATHY_TYPE_PROTOCOL_CHOOSER, NULL));
}

void
empathy_protocol_chooser_set_visible (EmpathyProtocolChooser *protocol_chooser,
    EmpathyProtocolChooserFilterFunc func,
    gpointer user_data)
{
  EmpathyProtocolChooserPriv *priv;
  GtkTreeModel *filter_model;

  g_return_if_fail (EMPATHY_IS_PROTOCOL_CHOOSER (protocol_chooser));

  priv = GET_PRIV (protocol_chooser);
  priv->filter_func = func;
  priv->filter_user_data = user_data;

  filter_model = gtk_tree_model_filter_new (GTK_TREE_MODEL (priv->store),
      NULL);
  gtk_combo_box_set_model (GTK_COMBO_BOX (protocol_chooser), filter_model);
  g_object_unref (filter_model);

  gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER
      (filter_model), protocol_chooser_filter_visible_func,
      protocol_chooser, NULL);

  gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (filter_model));

  gtk_combo_box_set_active (GTK_COMBO_BOX (protocol_chooser), 0);
}

EmpathyAccountSettings *
empathy_protocol_chooser_create_account_settings (EmpathyProtocolChooser *self)
{
  EmpathyAccountSettings *settings = NULL;
  gchar *str;
  const gchar *display_name;
  TpConnectionManager *cm;
  TpConnectionManagerProtocol *proto;
  gchar *service = NULL;

  cm = empathy_protocol_chooser_dup_selected (self, &proto, &service);
  if (cm == NULL || proto == NULL)
    goto out;

  if (service != NULL)
    display_name = empathy_service_name_to_display_name (service);
  else
    display_name = empathy_protocol_name_to_display_name (proto->name);

  /* Create account */
  /* To translator: %s is the name of the protocol, such as "Google Talk" or
   * "Yahoo!"
   */
  str = g_strdup_printf (_("New %s account"), display_name);
  settings = empathy_account_settings_new (cm->name, proto->name, service, str);

  g_free (str);

  if (!tp_strdiff (service, "google-talk"))
    {
      gchar *fallback_servers[] = {
          "talkx.l.google.com",
          "talkx.l.google.com:443,oldssl",
          "talkx.l.google.com:80",
          NULL};

      gchar *extra_certificate_identities[] = {
          "talk.google.com",
          NULL};

      empathy_account_settings_set_icon_name_async (settings, "im-google-talk",
          NULL, NULL);
      empathy_account_settings_set_string (settings, "server",
          extra_certificate_identities[0]);
      empathy_account_settings_set_boolean (settings, "require-encryption",
          TRUE);
      empathy_account_settings_set_strv (settings, "fallback-servers",
          fallback_servers);

      if (empathy_account_settings_have_tp_param (settings,
              "extra-certificate-identities"))
        {
          empathy_account_settings_set_strv (settings,
              "extra-certificate-identities", extra_certificate_identities);
        }
    }
  else if (!tp_strdiff (service, "facebook"))
    {
      empathy_account_settings_set_icon_name_async (settings, "im-facebook",
          NULL, NULL);
      empathy_account_settings_set_boolean (settings, "require-encryption",
          TRUE);
      empathy_account_settings_set_string (settings, "server",
          "chat.facebook.com");
    }

out:
  tp_clear_object (&cm);
  g_free (service);
  return settings;
}
