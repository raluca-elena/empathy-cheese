/*
 * Copyright (C) 2007-2008 Guillaume Desmottes
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
 *
 * Authors: Guillaume Desmottes <gdesmott@gnome.org>
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-irc-network-manager.h>

#include "empathy-irc-network-dialog.h"
#include "empathy-ui-utils.h"
#include "empathy-irc-network-chooser-dialog.h"

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT | EMPATHY_DEBUG_IRC
#include <libempathy/empathy-debug.h>

#include "empathy-irc-network-chooser.h"

#define DEFAULT_IRC_NETWORK "irc.gimp.org"
#define DEFAULT_IRC_PORT 6667
#define DEFAULT_IRC_SSL FALSE

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyIrcNetworkChooser)

enum {
    PROP_SETTINGS = 1
};

enum {
    SIG_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct {
    EmpathyAccountSettings *settings;

    EmpathyIrcNetworkManager *network_manager;
    GtkWidget *dialog;
    /* Displayed network */
    EmpathyIrcNetwork *network;
} EmpathyIrcNetworkChooserPriv;

G_DEFINE_TYPE (EmpathyIrcNetworkChooser, empathy_irc_network_chooser,
    GTK_TYPE_BUTTON);

static void
empathy_irc_network_chooser_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyIrcNetworkChooserPriv *priv = GET_PRIV (object);

  switch (prop_id)
    {
      case PROP_SETTINGS:
        priv->settings = g_value_dup_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
empathy_irc_network_chooser_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyIrcNetworkChooserPriv *priv = GET_PRIV (object);

  switch (prop_id)
    {
      case PROP_SETTINGS:
        g_value_set_object (value, priv->settings);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
unset_server_params (EmpathyIrcNetworkChooser *self)
{
  EmpathyIrcNetworkChooserPriv *priv = GET_PRIV (self);

  DEBUG ("Unset server, port and use-ssl");
  empathy_account_settings_unset (priv->settings, "server");
  empathy_account_settings_unset (priv->settings, "port");
  empathy_account_settings_unset (priv->settings, "use-ssl");
}

static void
update_server_params (EmpathyIrcNetworkChooser *self)
{
  EmpathyIrcNetworkChooserPriv *priv = GET_PRIV (self);
  GSList *servers;
  const gchar *charset;

  g_assert (priv->network != NULL);

  charset = empathy_irc_network_get_charset (priv->network);
  DEBUG ("Setting charset to %s", charset);
  empathy_account_settings_set_string (priv->settings, "charset", charset);

  servers = empathy_irc_network_get_servers (priv->network);
  if (g_slist_length (servers) > 0)
    {
      /* set the first server as CM server */
      EmpathyIrcServer *server = servers->data;
      gchar *address;
      guint port;
      gboolean ssl;

      g_object_get (server,
          "address", &address,
          "port", &port,
          "ssl", &ssl,
          NULL);

      DEBUG ("Setting server to %s", address);
      empathy_account_settings_set_string (priv->settings, "server", address);
      DEBUG ("Setting port to %u", port);
      empathy_account_settings_set_uint32 (priv->settings, "port", port);
      DEBUG ("Setting use-ssl to %s", ssl ? "TRUE": "FALSE" );
      empathy_account_settings_set_boolean (priv->settings, "use-ssl", ssl);

      g_free (address);
    }
  else
    {
      /* No server. Unset values */
      unset_server_params (self);
    }

  g_slist_foreach (servers, (GFunc) g_object_unref, NULL);
  g_slist_free (servers);
}

static void
set_label (EmpathyIrcNetworkChooser *self)
{
  EmpathyIrcNetworkChooserPriv *priv = GET_PRIV (self);

  g_assert (priv->network != NULL);

  gtk_button_set_label (GTK_BUTTON (self),
      empathy_irc_network_get_name (priv->network));
}

static void
set_label_from_settings (EmpathyIrcNetworkChooser *self)
{
  EmpathyIrcNetworkChooserPriv *priv = GET_PRIV (self);
  const gchar *server;

  tp_clear_object (&priv->network);

  server = empathy_account_settings_get_string (priv->settings, "server");

  if (server != NULL)
    {
      EmpathyIrcServer *srv;
      gint port;
      gboolean ssl;

      priv->network = empathy_irc_network_manager_find_network_by_address (
          priv->network_manager, server);

      if (priv->network != NULL)
        {
          /* The network is known */
          g_object_ref (priv->network);
          set_label (self);
          return;
        }

      /* We don't have this network. Let's create it */
      port = empathy_account_settings_get_uint32 (priv->settings, "port");
      ssl = empathy_account_settings_get_boolean (priv->settings,
          "use-ssl");

      DEBUG ("Create a network %s", server);
      priv->network = empathy_irc_network_new (server);
      srv = empathy_irc_server_new (server, port, ssl);

      empathy_irc_network_append_server (priv->network, srv);
      empathy_irc_network_manager_add (priv->network_manager, priv->network);

      set_label (self);

      g_object_unref (srv);
      return;
    }

  /* Set default network */
  priv->network = empathy_irc_network_manager_find_network_by_address (
          priv->network_manager, DEFAULT_IRC_NETWORK);

  if (priv->network == NULL)
    {
      /* Default network is not known, recreate it */
      EmpathyIrcServer *srv;

      priv->network = empathy_irc_network_new (DEFAULT_IRC_NETWORK);

      srv = empathy_irc_server_new (DEFAULT_IRC_NETWORK, DEFAULT_IRC_PORT,
          DEFAULT_IRC_SSL);

      empathy_irc_network_append_server (priv->network, srv);
      empathy_irc_network_manager_add (priv->network_manager, priv->network);

      g_object_unref (srv);
    }

  set_label (self);
  update_server_params (self);
  g_object_ref (priv->network);
}

static void
dialog_response_cb (GtkDialog *dialog,
    gint response,
    EmpathyIrcNetworkChooser *self)
{
  EmpathyIrcNetworkChooserPriv *priv = GET_PRIV (self);
  EmpathyIrcNetworkChooserDialog *chooser =
    EMPATHY_IRC_NETWORK_CHOOSER_DIALOG (priv->dialog);

  if (response != GTK_RESPONSE_CLOSE &&
      response != GTK_RESPONSE_DELETE_EVENT)
    return;

  if (empathy_irc_network_chooser_dialog_get_changed (chooser))
    {
      tp_clear_object (&priv->network);

      priv->network = g_object_ref (
          empathy_irc_network_chooser_dialog_get_network (chooser));

      update_server_params (self);
      set_label (self);

      g_signal_emit (self, signals[SIG_CHANGED], 0);
    }

  gtk_widget_destroy (priv->dialog);
  priv->dialog = NULL;
}

static void
clicked_cb (GtkButton *button,
    gpointer user_data)
{
  EmpathyIrcNetworkChooserPriv *priv = GET_PRIV (button);
  GtkWindow *window;

  if (priv->dialog != NULL)
    goto out;

  window = empathy_get_toplevel_window (GTK_WIDGET (button));

  priv->dialog = empathy_irc_network_chooser_dialog_new (priv->settings,
      priv->network, window);
  gtk_widget_show_all (priv->dialog);

  tp_g_signal_connect_object (priv->dialog, "response",
      G_CALLBACK (dialog_response_cb), button, 0);

out:
  empathy_window_present (GTK_WINDOW (priv->dialog));
}

static void
empathy_irc_network_chooser_constructed (GObject *object)
{
  EmpathyIrcNetworkChooser *self = (EmpathyIrcNetworkChooser *) object;
  EmpathyIrcNetworkChooserPriv *priv = GET_PRIV (self);

  g_assert (priv->settings != NULL);

  set_label_from_settings (self);

  g_signal_connect (self, "clicked", G_CALLBACK (clicked_cb), self);
}

static void
empathy_irc_network_chooser_dispose (GObject *object)
{
  EmpathyIrcNetworkManager *self = (EmpathyIrcNetworkManager *) object;
  EmpathyIrcNetworkChooserPriv *priv = GET_PRIV (self);

  tp_clear_object (&priv->settings);
  tp_clear_object (&priv->network_manager);
  tp_clear_object (&priv->network);

  if (G_OBJECT_CLASS (empathy_irc_network_chooser_parent_class)->dispose)
    G_OBJECT_CLASS (empathy_irc_network_chooser_parent_class)->dispose (object);
}

static void
empathy_irc_network_chooser_class_init (EmpathyIrcNetworkChooserClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = empathy_irc_network_chooser_get_property;
  object_class->set_property = empathy_irc_network_chooser_set_property;
  object_class->constructed = empathy_irc_network_chooser_constructed;
  object_class->dispose = empathy_irc_network_chooser_dispose;

  g_object_class_install_property (object_class, PROP_SETTINGS,
    g_param_spec_object ("settings",
      "Settings",
      "The EmpathyAccountSettings to show and edit",
      EMPATHY_TYPE_ACCOUNT_SETTINGS,
      G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  signals[SIG_CHANGED] = g_signal_new ("changed",
      G_OBJECT_CLASS_TYPE (object_class),
      G_SIGNAL_RUN_LAST,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE,
      0);

  g_type_class_add_private (object_class,
      sizeof (EmpathyIrcNetworkChooserPriv));
}

static void
empathy_irc_network_chooser_init (EmpathyIrcNetworkChooser *self)
{
  EmpathyIrcNetworkChooserPriv *priv;

  priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_IRC_NETWORK_CHOOSER, EmpathyIrcNetworkChooserPriv);
  self->priv = priv;

  priv->network_manager = empathy_irc_network_manager_dup_default ();
}

GtkWidget *
empathy_irc_network_chooser_new (EmpathyAccountSettings *settings)
{
  return g_object_new (EMPATHY_TYPE_IRC_NETWORK_CHOOSER,
      "settings", settings,
      NULL);
}

EmpathyIrcNetwork *
empathy_irc_network_chooser_get_network (EmpathyIrcNetworkChooser *self)
{
  EmpathyIrcNetworkChooserPriv *priv = GET_PRIV (self);

  return priv->network;
}
