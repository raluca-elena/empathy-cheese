/*
 * Copyright (C) 2007-2009 Collabora Ltd.
 * Copyright (C) 2007 Marco Barisione <marco@barisione.org>
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
 * Authors: Marco Barisione <marco@barisione.org>
 *          Jonny Lamb <jonny.lamb@collabora.co.uk>
 *          Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

#include <config.h>

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>

#include <glib/gi18n-lib.h>

#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>

#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/proxy-subclass.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/interfaces.h>

#include "empathy-tp-file.h"
#include "empathy-time.h"
#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_FT
#include "empathy-debug.h"

/**
 * SECTION:empathy-tp-file
 * @title: EmpathyTpFile
 * @short_description: Object which represents a Telepathy file channel
 * @include: libempathy/empathy-tp-file.h
 *
 * #EmpathyTpFile is an object which represents a Telepathy file channel.
 * Usually, clients do not need to deal with #EmpathyTpFile objects directly,
 * and are supposed to use #EmpathyFTHandler and #EmpathyFTFactory for
 * transferring files using libempathy.
 */

/* EmpathyTpFile object */

struct _EmpathyTpFilePrivate {
  GInputStream *in_stream;
  GOutputStream *out_stream;

  /* org.freedesktop.Telepathy.Channel.Type.FileTransfer D-Bus properties */
  TpFileTransferState state;
  TpFileTransferStateChangeReason state_change_reason;
  TpSocketAddressType socket_address_type;
  TpSocketAccessControl socket_access_control;

  /* transfer properties */
  gint64 start_time;
  GArray *socket_address;
  guint port;
  guint64 offset;

  /* GCancellable we're passed when offering/accepting the transfer */
  GCancellable *cancellable;

  /* callbacks for the operation */
  EmpathyTpFileProgressCallback progress_callback;
  gpointer progress_user_data;
  EmpathyTpFileOperationCallback op_callback;
  gpointer op_user_data;

  gboolean is_closing;
  gboolean is_closed;
};

G_DEFINE_TYPE (EmpathyTpFile, empathy_tp_file, TP_TYPE_FILE_TRANSFER_CHANNEL);

/* private functions */

static void
tp_file_get_state_cb (TpProxy *proxy,
    const GValue *value,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyTpFile *self = (EmpathyTpFile *) weak_object;

  if (error != NULL)
    {
      /* set a default value for the state */
      self->priv->state = TP_FILE_TRANSFER_STATE_NONE;
      return;
    }

  self->priv->state = g_value_get_uint (value);
}

static void
tp_file_get_available_socket_types_cb (TpProxy *proxy,
    const GValue *value,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyTpFile *self = (EmpathyTpFile *) weak_object;
  GHashTable *socket_types;
  GArray *access_controls;

  if (error != NULL ||
      !G_VALUE_HOLDS (value, TP_HASH_TYPE_SUPPORTED_SOCKET_MAP))
    {
      /* set a default value */
      self->priv->socket_address_type = TP_SOCKET_ADDRESS_TYPE_UNIX;
      self->priv->socket_access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
      goto out;
    }

  socket_types = g_value_get_boxed (value);

  /* here UNIX is preferred to IPV4 */
  if ((access_controls = g_hash_table_lookup (socket_types,
      GUINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_UNIX))) != NULL)
    {
      self->priv->socket_address_type = TP_SOCKET_ADDRESS_TYPE_UNIX;
      self->priv->socket_access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
      goto out;
    }

  if ((access_controls = g_hash_table_lookup (socket_types,
      GUINT_TO_POINTER (TP_SOCKET_ADDRESS_TYPE_IPV4))) != NULL)
    {
      self->priv->socket_address_type = TP_SOCKET_ADDRESS_TYPE_IPV4;

      /* TODO: we should prefer PORT over LOCALHOST when the CM will
       * support it.
       */

      self->priv->socket_access_control = TP_SOCKET_ACCESS_CONTROL_LOCALHOST;
    }

out:
  DEBUG ("Socket address type: %u, access control %u",
      self->priv->socket_address_type, self->priv->socket_access_control);
}

static void
tp_file_invalidated_cb (TpProxy       *proxy,
    guint          domain,
    gint           code,
    gchar         *message,
    EmpathyTpFile *self)
{
  DEBUG ("Channel invalidated: %s", message);

  if (self->priv->state != TP_FILE_TRANSFER_STATE_COMPLETED &&
      self->priv->state != TP_FILE_TRANSFER_STATE_CANCELLED)
    {
      /* The channel is not in a finished state, an error occured */
      self->priv->state = TP_FILE_TRANSFER_STATE_CANCELLED;
      self->priv->state_change_reason =
          TP_FILE_TRANSFER_STATE_CHANGE_REASON_LOCAL_ERROR;
    }
}

static void
ft_operation_close_clean (EmpathyTpFile *self)
{
  if (self->priv->is_closed)
    return;

  DEBUG ("FT operation close clean");

  self->priv->is_closed = TRUE;

  if (self->priv->op_callback != NULL)
    self->priv->op_callback (self, NULL, self->priv->op_user_data);
}

static void
ft_operation_close_with_error (EmpathyTpFile *self,
    GError *error)
{
  if (self->priv->is_closed)
    return;

  DEBUG ("FT operation close with error %s", error->message);

  self->priv->is_closed = TRUE;

  /* close the channel if it's not cancelled already */
  if (self->priv->state != TP_FILE_TRANSFER_STATE_CANCELLED)
    empathy_tp_file_cancel (self);

  if (self->priv->op_callback != NULL)
    self->priv->op_callback (self, error, self->priv->op_user_data);
}

static void
splice_stream_ready_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  EmpathyTpFile *self = user_data;
  GError *error = NULL;

  g_output_stream_splice_finish (G_OUTPUT_STREAM (source), res, &error);

  DEBUG ("Splice stream ready cb, error %p", error);

  if (error != NULL && !self->priv->is_closing)
    {
      ft_operation_close_with_error (self, error);
      g_clear_error (&error);
      return;
    }
}

static void
tp_file_start_transfer (EmpathyTpFile *self)
{
  gint fd, domain, res = 0;
  GError *error = NULL;
  struct sockaddr *my_addr = NULL;
  size_t my_size = 0;

  if (self->priv->socket_address_type == TP_SOCKET_ADDRESS_TYPE_UNIX)
    {
      domain = AF_UNIX;
    }
  else if (self->priv->socket_address_type == TP_SOCKET_ADDRESS_TYPE_IPV4)
    {
      domain = AF_INET;
    }
  else
    {
      error = g_error_new_literal (EMPATHY_FT_ERROR_QUARK,
          EMPATHY_FT_ERROR_NOT_SUPPORTED, _("Socket type not supported"));

      DEBUG ("Socket not supported, closing channel");

      ft_operation_close_with_error (self, error);
      g_clear_error (&error);

      return;
    }

  fd = socket (domain, SOCK_STREAM, 0);

  if (fd < 0)
    {
      int code = errno;

      error = g_error_new_literal (EMPATHY_FT_ERROR_QUARK,
          EMPATHY_FT_ERROR_SOCKET, g_strerror (code));

      DEBUG ("Failed to create socket, closing channel");

      ft_operation_close_with_error (self, error);
      g_clear_error (&error);

      return;
    }

  if (self->priv->socket_address_type == TP_SOCKET_ADDRESS_TYPE_UNIX)
    {
      struct sockaddr_un addr;

      memset (&addr, 0, sizeof (addr));
      addr.sun_family = domain;
      strncpy (addr.sun_path, self->priv->socket_address->data,
          self->priv->socket_address->len);

      my_addr = (struct sockaddr *) &addr;
      my_size = sizeof (addr);
    }
  else if (self->priv->socket_address_type == TP_SOCKET_ADDRESS_TYPE_IPV4)
    {
      struct sockaddr_in addr;

      memset (&addr, 0, sizeof (addr));
      addr.sin_family = domain;
      inet_pton (AF_INET, self->priv->socket_address->data, &addr.sin_addr);
      addr.sin_port = htons (self->priv->port);

      my_addr = (struct sockaddr *) &addr;
      my_size = sizeof (addr);
    }

  res = connect (fd, my_addr, my_size);

  if (res < 0)
    {
      int code = errno;

      error = g_error_new_literal (EMPATHY_FT_ERROR_QUARK,
          EMPATHY_FT_ERROR_SOCKET, g_strerror (code));

      DEBUG ("Failed to connect socket, closing channel");

      ft_operation_close_with_error (self, error);
      close (fd);
      g_clear_error (&error);

      return;
    }

  DEBUG ("Start the transfer");

  self->priv->start_time = empathy_time_get_current ();

  /* notify we're starting a transfer */
  if (self->priv->progress_callback != NULL)
    self->priv->progress_callback (self, 0, self->priv->progress_user_data);

  if (!tp_channel_get_requested ((TpChannel *) self))
    {
      GInputStream *socket_stream;

      socket_stream = g_unix_input_stream_new (fd, TRUE);

      g_output_stream_splice_async (self->priv->out_stream, socket_stream,
          G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
          G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
          G_PRIORITY_DEFAULT, self->priv->cancellable,
          splice_stream_ready_cb, self);

      g_object_unref (socket_stream);
    }
  else
    {
      GOutputStream *socket_stream;

      socket_stream = g_unix_output_stream_new (fd, TRUE);

      g_output_stream_splice_async (socket_stream, self->priv->in_stream,
          G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
          G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
          G_PRIORITY_DEFAULT, self->priv->cancellable,
          splice_stream_ready_cb, self);

      g_object_unref (socket_stream);
    }
}

static GError *
error_from_state_change_reason (TpFileTransferStateChangeReason reason)
{
  const char *string;
  GError *retval = NULL;

  string = NULL;

  switch (reason)
    {
      case TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE:
        string = _("No reason was specified");
        break;
      case TP_FILE_TRANSFER_STATE_CHANGE_REASON_REQUESTED:
        string = _("The change in state was requested");
        break;
      case TP_FILE_TRANSFER_STATE_CHANGE_REASON_LOCAL_STOPPED:
        string = _("You canceled the file transfer");
        break;
      case TP_FILE_TRANSFER_STATE_CHANGE_REASON_REMOTE_STOPPED:
        string = _("The other participant canceled the file transfer");
        break;
      case TP_FILE_TRANSFER_STATE_CHANGE_REASON_LOCAL_ERROR:
        string = _("Error while trying to transfer the file");
        break;
      case TP_FILE_TRANSFER_STATE_CHANGE_REASON_REMOTE_ERROR:
        string = _("The other participant is unable to transfer the file");
        break;
      default:
        string = _("Unknown reason");
        break;
    }

  retval = g_error_new_literal (EMPATHY_FT_ERROR_QUARK,
      EMPATHY_FT_ERROR_TP_ERROR, string);

  return retval;
}

static void
tp_file_state_changed_cb (TpChannel *proxy,
    guint state,
    guint reason,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyTpFile *self = (EmpathyTpFile *) weak_object;
  GError *error = NULL;

  if (state == self->priv->state)
    return;

  DEBUG ("File transfer state changed:\n"
      "old state = %u, state = %u, reason = %u\n"
      "\tincoming = %s, in_stream = %s, out_stream = %s",
      self->priv->state, state, reason,
      tp_channel_get_requested (proxy) ? "no" : "yes",
      self->priv->in_stream ? "present" : "not present",
      self->priv->out_stream ? "present" : "not present");

  self->priv->state = state;
  self->priv->state_change_reason = reason;

  /* If the channel is open AND we have the socket path, we can start the
   * transfer. The socket path could be NULL if we are not doing the actual
   * data transfer but are just an observer for the channel.
   */
  if (state == TP_FILE_TRANSFER_STATE_OPEN &&
      self->priv->socket_address != NULL)
    tp_file_start_transfer (EMPATHY_TP_FILE (weak_object));

  if (state == TP_FILE_TRANSFER_STATE_COMPLETED)
    ft_operation_close_clean (EMPATHY_TP_FILE (weak_object));

  if (state == TP_FILE_TRANSFER_STATE_CANCELLED)
    {
      error = error_from_state_change_reason (self->priv->state_change_reason);
      ft_operation_close_with_error (EMPATHY_TP_FILE (weak_object), error);
      g_clear_error (&error);
    }
}

static void
tp_file_transferred_bytes_changed_cb (TpFileTransferChannel *channel,
    GParamSpec *spec,
    EmpathyTpFile *self)
{
  guint64 count;

  count = tp_file_transfer_channel_get_transferred_bytes (channel);

  /* don't notify for 0 bytes count */
  if (count == 0)
    return;

  /* notify clients */
  if (self->priv->progress_callback != NULL)
    self->priv->progress_callback (self, count, self->priv->progress_user_data);
}

static void
ft_operation_provide_or_accept_file_cb (TpChannel *proxy,
    const GValue *address,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyTpFile *self = EMPATHY_TP_FILE (weak_object);
  GError *myerr = NULL;

  g_cancellable_set_error_if_cancelled (self->priv->cancellable, &myerr);

  if (error != NULL)
    {
      if (myerr != NULL)
        {
          /* if we were both cancelled and failed when calling the method,
          * report the method error.
          */
          g_clear_error (&myerr);
        }

      myerr = g_error_copy (error);
    }

  if (myerr != NULL)
    {
      DEBUG ("Error: %s", myerr->message);
      ft_operation_close_with_error (self, myerr);
      g_clear_error (&myerr);
      return;
    }

  if (G_VALUE_TYPE (address) == DBUS_TYPE_G_UCHAR_ARRAY)
    {
      self->priv->socket_address = g_value_dup_boxed (address);
    }
  else if (G_VALUE_TYPE (address) == G_TYPE_STRING)
    {
      /* Old bugged version of telepathy-salut used to store the address
       * as a 's' instead of an 'ay' */
      const gchar *path;

      path = g_value_get_string (address);
      self->priv->socket_address = g_array_sized_new (TRUE, FALSE,
          sizeof (gchar), strlen (path));
      g_array_insert_vals (self->priv->socket_address, 0, path, strlen (path));
    }
  else if (G_VALUE_TYPE (address) == TP_STRUCT_TYPE_SOCKET_ADDRESS_IPV4)
    {
      GValueArray *val_array;
      GValue *v;
      const char *addr;

      val_array = g_value_get_boxed (address);

      /* IPV4 address */
      v = g_value_array_get_nth (val_array, 0);
      addr = g_value_get_string (v);
      self->priv->socket_address = g_array_sized_new (TRUE, FALSE,
          sizeof (gchar), strlen (addr));
      g_array_insert_vals (self->priv->socket_address, 0, addr, strlen (addr));

      /* port number */
      v = g_value_array_get_nth (val_array, 1);
      self->priv->port = g_value_get_uint (v);
    }

  DEBUG ("Got socket address: %s, port (not zero if IPV4): %d",
      self->priv->socket_address->data, self->priv->port);

  /* if the channel is already open, start the transfer now, otherwise,
   * wait for the state change signal.
   */
  if (self->priv->state == TP_FILE_TRANSFER_STATE_OPEN)
    tp_file_start_transfer (self);
}

static void
initialize_empty_ac_variant (TpSocketAccessControl ac,
    GValue *val)
{
  /* TODO: we will add more types here once we support PORT access control. */
  if (ac == TP_SOCKET_ACCESS_CONTROL_LOCALHOST)
    {
      g_value_init (val, G_TYPE_STRING);
      g_value_set_static_string (val, "");
    }
}

static void
file_read_async_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  GValue nothing = { 0 };
  EmpathyTpFile *self = user_data;
  GFileInputStream *in_stream;
  GError *error = NULL;

  in_stream = g_file_read_finish (G_FILE (source), res, &error);

  if (error != NULL && !self->priv->is_closing)
    {
      ft_operation_close_with_error (self, error);
      g_clear_error (&error);
      return;
    }

  self->priv->in_stream = G_INPUT_STREAM (in_stream);

  /* we don't impose specific interface/port requirements even
   * if we're not using UNIX sockets.
   */
  initialize_empty_ac_variant (self->priv->socket_access_control, &nothing);

  tp_cli_channel_type_file_transfer_call_provide_file ((TpChannel *) self, -1,
      self->priv->socket_address_type, self->priv->socket_access_control,
      &nothing, ft_operation_provide_or_accept_file_cb,
      NULL, NULL, G_OBJECT (self));
}

static void
file_transfer_set_uri_cb (TpProxy *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GValue nothing = { 0 };
  EmpathyTpFile *self = (EmpathyTpFile *) weak_object;

  if (error != NULL)
    {
      DEBUG ("Failed to set FileTransfer.URI: %s", error->message);
      /* We don't return as that's not a big issue */
    }

  /* we don't impose specific interface/port requirements even
   * if we're not using UNIX sockets.
   */
  initialize_empty_ac_variant (self->priv->socket_access_control, &nothing);

  tp_cli_channel_type_file_transfer_call_accept_file ((TpChannel *) self,
      -1, self->priv->socket_address_type, self->priv->socket_access_control,
      &nothing, self->priv->offset,
      ft_operation_provide_or_accept_file_cb, NULL, NULL, weak_object);
}

static void
file_replace_async_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  EmpathyTpFile *self = user_data;
  GError *error = NULL;
  GFileOutputStream *out_stream;
  GFile *file = G_FILE (source);
  gchar *uri;
  GValue *value;

  out_stream = g_file_replace_finish (file, res, &error);

  if (error != NULL)
    {
      ft_operation_close_with_error (self, error);
      g_clear_error (&error);

      return;
    }

  self->priv->out_stream = G_OUTPUT_STREAM (out_stream);

  /* Try setting FileTranfer.URI before accepting the file */
  uri = g_file_get_uri (file);
  value = tp_g_value_slice_new_take_string (uri);

  tp_cli_dbus_properties_call_set ((TpChannel *) self, -1,
      TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "URI", value,
      file_transfer_set_uri_cb, NULL, NULL, G_OBJECT (self));

  tp_g_value_slice_free (value);
}

static void
channel_closed_cb (TpChannel *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyTpFile *self = EMPATHY_TP_FILE (weak_object);
  gboolean cancel = GPOINTER_TO_INT (user_data);

  DEBUG ("Channel is closed, should cancel %s", cancel ? "True" : "False");

  if (self->priv->cancellable != NULL &&
      !g_cancellable_is_cancelled (self->priv->cancellable) && cancel)
    g_cancellable_cancel (self->priv->cancellable);
}

static void
close_channel_internal (EmpathyTpFile *self,
    gboolean cancel)
{
  DEBUG ("Closing channel, should cancel %s", cancel ?
         "True" : "False");

  self->priv->is_closing = TRUE;

  tp_cli_channel_call_close ((TpChannel *) self, -1,
    channel_closed_cb, GINT_TO_POINTER (cancel), NULL, G_OBJECT (self));
}

/* GObject methods */

static void
empathy_tp_file_init (EmpathyTpFile *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
      EMPATHY_TYPE_TP_FILE, EmpathyTpFilePrivate);
}

static void
do_dispose (GObject *object)
{
  EmpathyTpFile *self = (EmpathyTpFile *) object;

  tp_clear_object (&self->priv->in_stream);
  tp_clear_object (&self->priv->out_stream);
  tp_clear_object (&self->priv->cancellable);

  G_OBJECT_CLASS (empathy_tp_file_parent_class)->dispose (object);
}

static void
do_finalize (GObject *object)
{
  EmpathyTpFile *self = (EmpathyTpFile *) object;

  DEBUG ("%p", object);

  if (self->priv->socket_address != NULL)
    {
      g_array_unref (self->priv->socket_address);
      self->priv->socket_address = NULL;
    }

  G_OBJECT_CLASS (empathy_tp_file_parent_class)->finalize (object);
}

static void
do_constructed (GObject *object)
{
  EmpathyTpFile *self = (EmpathyTpFile *) object;
  TpChannel *channel = (TpChannel *) self;

  g_signal_connect (self, "invalidated",
    G_CALLBACK (tp_file_invalidated_cb), self);

  tp_cli_channel_type_file_transfer_connect_to_file_transfer_state_changed (
      channel, tp_file_state_changed_cb, NULL, NULL, object, NULL);

  g_signal_connect (self, "notify::transferred-bytes",
    G_CALLBACK (tp_file_transferred_bytes_changed_cb), self);

  tp_cli_dbus_properties_call_get (channel,
      -1, TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "State", tp_file_get_state_cb,
      NULL, NULL, object);

  tp_cli_dbus_properties_call_get (channel,
      -1, TP_IFACE_CHANNEL_TYPE_FILE_TRANSFER, "AvailableSocketTypes",
      tp_file_get_available_socket_types_cb, NULL, NULL, object);

  self->priv->state_change_reason =
      TP_FILE_TRANSFER_STATE_CHANGE_REASON_NONE;

  G_OBJECT_CLASS (empathy_tp_file_parent_class)->constructed (object);
}

static void
empathy_tp_file_class_init (EmpathyTpFileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = do_finalize;
  object_class->dispose = do_dispose;
  object_class->constructed = do_constructed;

  g_type_class_add_private (object_class, sizeof (EmpathyTpFilePrivate));
}

/* public methods */

EmpathyTpFile *
empathy_tp_file_new (TpSimpleClientFactory *factory,
    TpConnection *conn,
    const gchar *object_path,
    const GHashTable *immutable_properties,
    GError **error)
{
  TpProxy *conn_proxy = (TpProxy *) conn;

  g_return_val_if_fail (TP_IS_CONNECTION (conn), NULL);
  g_return_val_if_fail (immutable_properties != NULL, NULL);

  return g_object_new (EMPATHY_TYPE_TP_FILE,
           "factory", factory,
           "connection", conn,
           "dbus-daemon", conn_proxy->dbus_daemon,
           "bus-name", conn_proxy->bus_name,
           "object-path", object_path,
           "channel-properties", immutable_properties,
           NULL);
}

/**
 * empathy_tp_file_accept:
 * @self: an incoming #EmpathyTpFile
 * @offset: the offset of @gfile where we should start writing
 * @gfile: the destination #GFile for the transfer
 * @cancellable: a #GCancellable
 * @progress_callback: function to callback with progress information
 * @progress_user_data: user_data to pass to @progress_callback
 * @op_callback: function to callback when the transfer ends
 * @op_user_data: user_data to pass to @op_callback
 *
 * Accepts an incoming file transfer, saving the result into @gfile.
 * The callback @op_callback will be called both when the transfer is
 * successful and in case of an error. Note that cancelling @cancellable,
 * closes the socket of the file operation in progress, but doesn't
 * guarantee that the transfer channel will be closed as well. Thus,
 * empathy_tp_file_cancel() or empathy_tp_file_close() should be used to
 * actually cancel an ongoing #EmpathyTpFile.
 */
void
empathy_tp_file_accept (EmpathyTpFile *self,
    guint64 offset,
    GFile *gfile,
    GCancellable *cancellable,
    EmpathyTpFileProgressCallback progress_callback,
    gpointer progress_user_data,
    EmpathyTpFileOperationCallback op_callback,
    gpointer op_user_data)
{
  g_return_if_fail (EMPATHY_IS_TP_FILE (self));
  g_return_if_fail (G_IS_FILE (gfile));
  g_return_if_fail (G_IS_CANCELLABLE (cancellable));

  self->priv->cancellable = g_object_ref (cancellable);
  self->priv->progress_callback = progress_callback;
  self->priv->progress_user_data = progress_user_data;
  self->priv->op_callback = op_callback;
  self->priv->op_user_data = op_user_data;
  self->priv->offset = offset;

  g_file_replace_async (gfile, NULL, FALSE, G_FILE_CREATE_NONE,
      G_PRIORITY_DEFAULT, cancellable, file_replace_async_cb, self);
}


/**
 * empathy_tp_file_offer:
 * @self: an outgoing #EmpathyTpFile
 * @gfile: the source #GFile for the transfer
 * @cancellable: a #GCancellable
 * @progress_callback: function to callback with progress information
 * @progress_user_data: user_data to pass to @progress_callback
 * @op_callback: function to callback when the transfer ends
 * @op_user_data: user_data to pass to @op_callback
 *
 * Offers an outgoing file transfer, reading data from @gfile.
 * The callback @op_callback will be called both when the transfer is
 * successful and in case of an error. Note that cancelling @cancellable,
 * closes the socket of the file operation in progress, but doesn't
 * guarantee that the transfer channel will be closed as well. Thus,
 * empathy_tp_file_cancel() or empathy_tp_file_close() should be used to
 * actually cancel an ongoing #EmpathyTpFile.
 */
void
empathy_tp_file_offer (EmpathyTpFile *self,
    GFile *gfile,
    GCancellable *cancellable,
    EmpathyTpFileProgressCallback progress_callback,
    gpointer progress_user_data,
    EmpathyTpFileOperationCallback op_callback,
    gpointer op_user_data)
{
  g_return_if_fail (EMPATHY_IS_TP_FILE (self));
  g_return_if_fail (G_IS_FILE (gfile));
  g_return_if_fail (G_IS_CANCELLABLE (cancellable));

  self->priv->cancellable = g_object_ref (cancellable);
  self->priv->progress_callback = progress_callback;
  self->priv->progress_user_data = progress_user_data;
  self->priv->op_callback = op_callback;
  self->priv->op_user_data = op_user_data;

  g_file_read_async (gfile, G_PRIORITY_DEFAULT, cancellable,
      file_read_async_cb, self);
}

/**
 * empathy_tp_file_cancel:
 * @self: an #EmpathyTpFile
 *
 * Cancels an ongoing #EmpathyTpFile, first closing the channel and then
 * cancelling any I/O operation and closing the socket.
 */
void
empathy_tp_file_cancel (EmpathyTpFile *self)
{
  g_return_if_fail (EMPATHY_IS_TP_FILE (self));

  close_channel_internal (self, TRUE);
}

/**
 * empathy_tp_file_close:
 * @self: an #EmpathyTpFile
 *
 * Closes the channel for an ongoing #EmpathyTpFile. It's safe to call this
 * method after the transfer has ended.
 */
void
empathy_tp_file_close (EmpathyTpFile *self)
{
  g_return_if_fail (EMPATHY_IS_TP_FILE (self));

  close_channel_internal (self, FALSE);
}
