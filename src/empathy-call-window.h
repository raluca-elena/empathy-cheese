/*
 * empathy-call-window.h - Header for EmpathyCallWindow
 * Copyright (C) 2008-2011 Collabora Ltd.
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

#ifndef __EMPATHY_CALL_WINDOW_H__
#define __EMPATHY_CALL_WINDOW_H__

#include <glib-object.h>
#include <gtk/gtk.h>

#include "empathy-call-handler.h"
#include "empathy-audio-src.h"
#include "empathy-video-src.h"

G_BEGIN_DECLS

typedef struct _EmpathyCallWindow EmpathyCallWindow;
typedef struct _EmpathyCallWindowPriv EmpathyCallWindowPriv;
typedef struct _EmpathyCallWindowClass EmpathyCallWindowClass;

struct _EmpathyCallWindowClass {
    GtkWindowClass parent_class;
};

struct _EmpathyCallWindow {
    GtkWindow parent;
    EmpathyCallWindowPriv *priv;
};

GType empathy_call_window_get_type (void);

/* TYPE MACROS */
#define EMPATHY_TYPE_CALL_WINDOW \
  (empathy_call_window_get_type ())
#define EMPATHY_CALL_WINDOW(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EMPATHY_TYPE_CALL_WINDOW, \
    EmpathyCallWindow))
#define EMPATHY_CALL_WINDOW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EMPATHY_TYPE_CALL_WINDOW, \
    EmpathyCallWindowClass))
#define EMPATHY_IS_CALL_WINDOW(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EMPATHY_TYPE_CALL_WINDOW))
#define EMPATHY_IS_CALL_WINDOW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EMPATHY_TYPE_CALL_WINDOW))
#define EMPATHY_CALL_WINDOW_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_CALL_WINDOW, \
    EmpathyCallWindowClass))

EmpathyCallWindow *empathy_call_window_new (EmpathyCallHandler *handler);
void empathy_call_window_present (EmpathyCallWindow *window,
  EmpathyCallHandler *handler);
void empathy_call_window_start_ringing (EmpathyCallWindow *self,
  TpyCallChannel *channel,
  TpChannelDispatchOperation *dispatch_operation,
  TpAddDispatchOperationContext *context);

GtkUIManager *empathy_call_window_get_ui_manager (EmpathyCallWindow *window);

EmpathyGstAudioSrc *empathy_call_window_get_audio_src (EmpathyCallWindow *window);
EmpathyGstVideoSrc *empathy_call_window_get_video_src (EmpathyCallWindow *window);

void empathy_call_window_play_camera (EmpathyCallWindow *self,
    gboolean play);

void empathy_call_window_change_webcam (EmpathyCallWindow *self,
    const gchar *device);

G_END_DECLS

#endif /* #ifndef __EMPATHY_CALL_WINDOW_H__*/
