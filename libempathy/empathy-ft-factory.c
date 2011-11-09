/*
 * empathy-ft-factory.c - Source for EmpathyFTFactory
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
 * Author: Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

/* empathy-ft-factory.c */

#include <glib.h>

#include <telepathy-glib/telepathy-glib.h>

#include "empathy-ft-factory.h"
#include "empathy-ft-handler.h"
#include "empathy-request-util.h"
#include "empathy-utils.h"

/**
 * SECTION:empathy-ft-factory
 * @title:EmpathyFTFactory
 * @short_description: creates #EmpathyFTHandler objects
 * @include: libempathy/empathy-ft-factory.h
 *
 * #EmpathyFTFactory takes care of the creation of the #EmpathyFTHandler
 * objects used for file transfer. As the creation of the handlers is
 * async, a client will have to connect to the ::new-ft-handler signal
 * to receive the handler.
 * In case of an incoming file transfer, the handler will need the destination
 * file before being useful; as this is usually decided by the user (e.g. with
 * a file selector), a ::new-incoming-transfer is emitted by the factory when
 * a destination file is needed, which can be set later with
 * empathy_ft_factory_set_destination_for_incoming_handler().
 */

G_DEFINE_TYPE (EmpathyFTFactory, empathy_ft_factory, G_TYPE_OBJECT);

enum {
  NEW_FT_HANDLER,
  NEW_INCOMING_TRANSFER,
  LAST_SIGNAL
};

static EmpathyFTFactory *factory_singleton = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

/* private structure */
typedef struct {
  TpBaseClient *handler;
} EmpathyFTFactoryPriv;

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyFTFactory)

static GObject *
do_constructor (GType type,
    guint n_props,
    GObjectConstructParam *props)
{
  GObject *retval;

  if (factory_singleton != NULL)
    {
      retval = g_object_ref (factory_singleton);
    }
  else
    {
      retval = G_OBJECT_CLASS (empathy_ft_factory_parent_class)->constructor
        (type, n_props, props);

      factory_singleton = EMPATHY_FT_FACTORY (retval);
      g_object_add_weak_pointer (retval, (gpointer *) &factory_singleton);
    }

  return retval;
}

static void
empathy_ft_factory_dispose (GObject *object)
{
  EmpathyFTFactory *self = (EmpathyFTFactory *) object;
  EmpathyFTFactoryPriv *priv = GET_PRIV (self);

  tp_clear_object (&priv->handler);

  (G_OBJECT_CLASS (empathy_ft_factory_parent_class)->dispose) (object);
}

static void
empathy_ft_factory_class_init (EmpathyFTFactoryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (EmpathyFTFactoryPriv));

  object_class->constructor = do_constructor;
  object_class->dispose = empathy_ft_factory_dispose;

  /**
   * EmpathyFTFactory::new-ft-handler
   * @factory: the object which received the signal
   * @handler: the handler made available by the factory
   * @error: a #GError or %NULL
   *
   * The signal is emitted when a new #EmpathyFTHandler is available.
   * Note that @handler is never %NULL even if @error is set, as you might want
   * to display the error in an UI; in that case, the handler won't support
   * any transfer.
   */
  signals[NEW_FT_HANDLER] =
    g_signal_new ("new-ft-handler",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0,
      NULL, NULL,
      g_cclosure_marshal_generic,
      G_TYPE_NONE, 2, EMPATHY_TYPE_FT_HANDLER, G_TYPE_POINTER);

  /**
   * EmpathyFTFactory::new-incoming-transfer
   * @factory: the object which received the signal
   * @handler: the incoming handler being constructed
   * @error: a #GError or %NULL
   *
   * The signal is emitted when a new incoming #EmpathyFTHandler is being
   * constructed, and needs a destination #GFile to be useful.
   * Clients that connect to this signal will have to call
   * empathy_ft_factory_set_destination_for_incoming_handler() when they
   * have a #GFile.
   * Note that @handler is never %NULL even if @error is set, as you might want
   * to display the error in an UI; in that case, the handler won't support
   * any transfer.
   */
  signals[NEW_INCOMING_TRANSFER] =
    g_signal_new ("new-incoming-transfer",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0,
      NULL, NULL,
      g_cclosure_marshal_generic,
      G_TYPE_NONE, 2, EMPATHY_TYPE_FT_HANDLER, G_TYPE_POINTER);
}

static void
ft_handler_incoming_ready_cb (EmpathyFTHandler *handler,
    GError *error,
    gpointer user_data)
{
  EmpathyFTFactory *factory = user_data;

  g_signal_emit (factory, signals[NEW_INCOMING_TRANSFER], 0, handler, error);
}

static void
handle_channels_cb (TpSimpleHandler *handler,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    GList *requests_satisfied,
    gint64 user_action_time,
    TpHandleChannelsContext *context,
    gpointer user_data)
{
  EmpathyFTFactory *self = user_data;
  GList *l;

  for (l = channels; l != NULL; l = g_list_next (l))
    {
      TpChannel *channel = l->data;

      if (tp_proxy_get_invalidated (channel) != NULL)
        continue;

      if (!EMPATHY_IS_TP_FILE (channel))
        continue;

      /* We handle only incoming FT */
      empathy_ft_handler_new_incoming ((TpFileTransferChannel *) channel,
          ft_handler_incoming_ready_cb, self);
    }


  tp_handle_channels_context_accept (context);
}

static void
empathy_ft_factory_init (EmpathyFTFactory *self)
{
  EmpathyFTFactoryPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
    EMPATHY_TYPE_FT_FACTORY, EmpathyFTFactoryPriv);
  TpAccountManager *am;

  self->priv = priv;

  am = tp_account_manager_dup ();

  priv->handler = tp_simple_handler_new_with_am (am, FALSE, FALSE,
      EMPATHY_FT_BUS_NAME_SUFFIX, FALSE, handle_channels_cb, self, NULL);

  tp_base_client_take_handler_filter (priv->handler, tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
        /* Only handle *incoming* channels as outgoing FT channels has to be
         * handled by the requester. */
        TP_PROP_CHANNEL_REQUESTED, G_TYPE_BOOLEAN, FALSE,
        NULL));

  g_object_unref (am);
}

static void
ft_handler_outgoing_ready_cb (EmpathyFTHandler *handler,
    GError *error,
    gpointer user_data)
{
  EmpathyFTFactory *factory = user_data;

  g_signal_emit (factory, signals[NEW_FT_HANDLER], 0, handler, error);
}

/* public methods */

/**
 * empathy_ft_factory_dup_singleton:
 *
 * Gives the caller a reference to the #EmpathyFTFactory singleton,
 * (creating it if necessary).
 *
 * Return value: an #EmpathyFTFactory object
 */
EmpathyFTFactory *
empathy_ft_factory_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_FT_FACTORY, NULL);
}

/**
 * empathy_ft_factory_new_transfer_outgoing:
 * @factory: an #EmpathyFTFactory
 * @contact: the #EmpathyContact destination of the transfer
 * @source: the #GFile to be transferred to @contact
 *
 * Trigger the creation of an #EmpathyFTHandler object to send @source to
 * the specified @contact.
 */
void
empathy_ft_factory_new_transfer_outgoing (EmpathyFTFactory *factory,
    EmpathyContact *contact,
    GFile *source,
    gint64 action_time)
{
  g_return_if_fail (EMPATHY_IS_FT_FACTORY (factory));
  g_return_if_fail (EMPATHY_IS_CONTACT (contact));
  g_return_if_fail (G_IS_FILE (source));

  empathy_ft_handler_new_outgoing (contact, source, action_time,
      ft_handler_outgoing_ready_cb, factory);
}

/**
 * empathy_ft_factory_set_destination_for_incoming_handler:
 * @factory: an #EmpathyFTFactory
 * @handler: the #EmpathyFTHandler to set the destination of
 * @destination: the #GFile destination of the transfer
 *
 * Sets @destination as destination file for the transfer. After the call of
 * this method, the ::new-ft-handler will be emitted for the incoming handler.
 */
void
empathy_ft_factory_set_destination_for_incoming_handler (
    EmpathyFTFactory *factory,
    EmpathyFTHandler *handler,
    GFile *destination)
{
  g_return_if_fail (EMPATHY_IS_FT_FACTORY (factory));
  g_return_if_fail (EMPATHY_IS_FT_HANDLER (handler));
  g_return_if_fail (G_IS_FILE (destination));

  empathy_ft_handler_incoming_set_destination (handler, destination);

  g_signal_emit (factory, signals[NEW_FT_HANDLER], 0, handler, NULL);
}

gboolean
empathy_ft_factory_register (EmpathyFTFactory *self,
    GError **error)
{
  EmpathyFTFactoryPriv *priv = GET_PRIV (self);

  return tp_base_client_register (priv->handler, error);
}
