/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007 Elliot Fairweather
 * Copyright (C) 2007-2008 Collabora Ltd.
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
 * Authors: Elliot Fairweather <elliot.fairweather@collabora.co.uk>
 *          Xavier Claessens <xclaesse@gmail.com>
 */

#ifndef __EMPATHY_TP_STREAMED_MEDIA_H__
#define __EMPATHY_TP_STREAMED_MEDIA_H__

#include <glib.h>
#include <telepathy-glib/channel.h>

#include "empathy-contact.h"

G_BEGIN_DECLS

#define EMPATHY_TYPE_TP_STREAMED_MEDIA (empathy_tp_streamed_media_get_type ())
#define EMPATHY_TP_STREAMED_MEDIA(object) (G_TYPE_CHECK_INSTANCE_CAST ((object), \
    EMPATHY_TYPE_TP_STREAMED_MEDIA, EmpathyTpStreamedMedia))
#define EMPATHY_TP_STREAMED_MEDIA_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), \
    EMPATHY_TYPE_TP_STREAMED_MEDIA, EmpathyTpStreamedMediaClass))
#define EMPATHY_IS_TP_STREAMED_MEDIA(object) (G_TYPE_CHECK_INSTANCE_TYPE ((object), \
    EMPATHY_TYPE_TP_STREAMED_MEDIA))
#define EMPATHY_IS_TP_STREAMED_MEDIA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), \
    EMPATHY_TYPE_TP_STREAMED_MEDIA))
#define EMPATHY_TP_STREAMED_MEDIA_GET_CLASS(object) \
  (G_TYPE_INSTANCE_GET_CLASS ((object), \
    EMPATHY_TYPE_TP_STREAMED_MEDIA, EmpathyTpStreamedMediaClass))

typedef struct _EmpathyTpStreamedMedia EmpathyTpStreamedMedia;
typedef struct _EmpathyTpStreamedMediaClass EmpathyTpStreamedMediaClass;

struct _EmpathyTpStreamedMedia {
  GObject parent;
  gpointer priv;
};

struct _EmpathyTpStreamedMediaClass {
  GObjectClass parent_class;
};

typedef enum
{
  EMPATHY_TP_STREAMED_MEDIA_STATUS_READYING,
  EMPATHY_TP_STREAMED_MEDIA_STATUS_PENDING,
  EMPATHY_TP_STREAMED_MEDIA_STATUS_ACCEPTED,
  EMPATHY_TP_STREAMED_MEDIA_STATUS_CLOSED
} EmpathyTpStreamedMediaStatus;

typedef struct
{
  gboolean exists;
  guint id;
  guint state;
  guint direction;
} EmpathyTpStreamedMediaStream;

GType empathy_tp_streamed_media_get_type (void) G_GNUC_CONST;
EmpathyTpStreamedMedia *empathy_tp_streamed_media_new (TpAccount *account,
    TpChannel *channel);
void empathy_tp_streamed_media_close (EmpathyTpStreamedMedia *streamed_media);

void empathy_tp_streamed_media_accept_incoming_call (
    EmpathyTpStreamedMedia *streamed_media);
void empathy_tp_streamed_media_request_video_stream_direction (
    EmpathyTpStreamedMedia *streamed_media,
    gboolean is_sending);
void empathy_tp_streamed_media_start_tone (
    EmpathyTpStreamedMedia *streamed_media,
    TpDTMFEvent event);
void empathy_tp_streamed_media_stop_tone (
    EmpathyTpStreamedMedia *streamed_media);
gboolean empathy_tp_streamed_media_has_dtmf (
    EmpathyTpStreamedMedia *streamed_media);
gboolean empathy_tp_streamed_media_is_receiving_video (
    EmpathyTpStreamedMedia *streamed_media);
gboolean empathy_tp_streamed_media_is_sending_video (
    EmpathyTpStreamedMedia *streamed_media);

const gchar * empathy_tp_streamed_media_get_connection_manager (
    EmpathyTpStreamedMedia *self);

gboolean empathy_tp_streamed_media_has_initial_video (
    EmpathyTpStreamedMedia *self);

void empathy_tp_streamed_media_leave (EmpathyTpStreamedMedia *self);

EmpathyTpStreamedMediaStatus empathy_tp_streamed_media_get_status (
    EmpathyTpStreamedMedia *self);

TpAccount * empathy_tp_streamed_media_get_account (
    EmpathyTpStreamedMedia *self);

G_END_DECLS

#endif /* __EMPATHY_TP_STREAMED_MEDIA_H__ */
