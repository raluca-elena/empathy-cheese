/*
 * empathy-streamed-media-handler.h - Header for EmpathyStreamedMediaHandler
 * Copyright (C) 2008-2009 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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

#ifndef __EMPATHY_STREAMED_MEDIA_HANDLER_H__
#define __EMPATHY_STREAMED_MEDIA_HANDLER_H__

#include <glib-object.h>

#include <gst/gst.h>
#include <gst/farsight/fs-conference-iface.h>

#include <libempathy/empathy-tp-streamed-media.h>
#include <libempathy/empathy-contact.h>

G_BEGIN_DECLS

typedef struct _EmpathyStreamedMediaHandler EmpathyStreamedMediaHandler;
typedef struct _EmpathyStreamedMediaHandlerClass EmpathyStreamedMediaHandlerClass;

struct _EmpathyStreamedMediaHandlerClass {
    GObjectClass parent_class;
};

struct _EmpathyStreamedMediaHandler {
    GObject parent;
    gpointer priv;
};

GType empathy_streamed_media_handler_get_type (void);

/* TYPE MACROS */
#define EMPATHY_TYPE_STREAMED_MEDIA_HANDLER \
  (empathy_streamed_media_handler_get_type ())
#define EMPATHY_STREAMED_MEDIA_HANDLER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EMPATHY_TYPE_STREAMED_MEDIA_HANDLER, \
    EmpathyStreamedMediaHandler))
#define EMPATHY_STREAMED_MEDIA_HANDLER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EMPATHY_TYPE_STREAMED_MEDIA_HANDLER, \
  EmpathyStreamedMediaHandlerClass))
#define EMPATHY_IS_STREAMED_MEDIA_HANDLER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EMPATHY_TYPE_STREAMED_MEDIA_HANDLER))
#define EMPATHY_IS_STREAMED_MEDIA_HANDLER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EMPATHY_TYPE_STREAMED_MEDIA_HANDLER))
#define EMPATHY_STREAMED_MEDIA_HANDLER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_STREAMED_MEDIA_HANDLER, \
  EmpathyStreamedMediaHandlerClass))

EmpathyStreamedMediaHandler * empathy_streamed_media_handler_new_for_contact (
  EmpathyContact *contact);

EmpathyStreamedMediaHandler * empathy_streamed_media_handler_new_for_channel (
  EmpathyTpStreamedMedia *call);

void empathy_streamed_media_handler_start_call (EmpathyStreamedMediaHandler *handler,
    gint64 timestamp);
void empathy_streamed_media_handler_stop_call (EmpathyStreamedMediaHandler *handler);

gboolean empathy_streamed_media_handler_has_initial_video (EmpathyStreamedMediaHandler *handler);

void empathy_streamed_media_handler_bus_message (EmpathyStreamedMediaHandler *handler,
  GstBus *bus, GstMessage *message);

FsCodec * empathy_streamed_media_handler_get_send_audio_codec (
    EmpathyStreamedMediaHandler *self);

FsCodec * empathy_streamed_media_handler_get_send_video_codec (
    EmpathyStreamedMediaHandler *self);

GList * empathy_streamed_media_handler_get_recv_audio_codecs (
    EmpathyStreamedMediaHandler *self);

GList * empathy_streamed_media_handler_get_recv_video_codecs (
    EmpathyStreamedMediaHandler *self);

FsCandidate * empathy_streamed_media_handler_get_audio_remote_candidate (
    EmpathyStreamedMediaHandler *self);

FsCandidate * empathy_streamed_media_handler_get_audio_local_candidate (
    EmpathyStreamedMediaHandler *self);

FsCandidate * empathy_streamed_media_handler_get_video_remote_candidate (
    EmpathyStreamedMediaHandler *self);

FsCandidate * empathy_streamed_media_handler_get_video_local_candidate (
    EmpathyStreamedMediaHandler *self);

G_END_DECLS

#endif /* #ifndef __EMPATHY_STREAMED_MEDIA_HANDLER_H__*/
