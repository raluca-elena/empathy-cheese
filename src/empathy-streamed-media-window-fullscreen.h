/*
 * empathy-streamed-media-window-fullscreen.h - Header for EmpathyStreamedMediaWindowFullscreen
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
 */

#ifndef __EMPATHY_STREAMED_MEDIA_WINDOW_FULLSCREEN_H__
#define __EMPATHY_STREAMED_MEDIA_WINDOW_FULLSCREEN_H__

#include <glib-object.h>
#include <gtk/gtk.h>

#include "empathy-streamed-media-window.h"

G_BEGIN_DECLS

typedef struct _EmpathyStreamedMediaWindowFullscreen EmpathyStreamedMediaWindowFullscreen;
typedef struct _EmpathyStreamedMediaWindowFullscreenClass
    EmpathyStreamedMediaWindowFullscreenClass;

struct _EmpathyStreamedMediaWindowFullscreenClass {
  GObjectClass parent_class;
};

struct _EmpathyStreamedMediaWindowFullscreen {
  GObject parent;
  gboolean is_fullscreen;
  GtkWidget *leave_fullscreen_button;
};

GType empathy_streamed_media_window_fullscreen_get_type (void);

/* TYPE MACROS */
#define EMPATHY_TYPE_STREAMED_MEDIA_WINDOW_FULLSCREEN \
  (empathy_streamed_media_window_fullscreen_get_type ())
#define EMPATHY_STREAMED_MEDIA_WINDOW_FULLSCREEN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EMPATHY_TYPE_STREAMED_MEDIA_WINDOW_FULLSCREEN, \
    EmpathyStreamedMediaWindowFullscreen))
#define EMPATHY_STREAMED_MEDIA_WINDOW_FULLSCREEN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EMPATHY_TYPE_STREAMED_MEDIA_WINDOW_FULLSCREEN, \
    EmpathyStreamedMediaWindowClassFullscreen))
#define EMPATHY_IS_STREAMED_MEDIA_WINDOW_FULLSCREEN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EMPATHY_TYPE_STREAMED_MEDIA_WINDOW_FULLSCREEN))
#define EMPATHY_IS_STREAMED_MEDIA_WINDOW_FULLSCREEN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EMPATHY_TYPE_STREAMED_MEDIA_WINDOW_FULLSCREEN))
#define EMPATHY_STREAMED_MEDIA_WINDOW_FULLSCREEN_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_STREAMED_MEDIA_WINDOW_FULLSCREEN, \
    EmpathyStreamedMediaWindowFullscreenClass))

EmpathyStreamedMediaWindowFullscreen *empathy_streamed_media_window_fullscreen_new (
    EmpathyStreamedMediaWindow *parent);

void empathy_streamed_media_window_fullscreen_set_fullscreen (
    EmpathyStreamedMediaWindowFullscreen *fs,
    gboolean set_fullscreen);
void empathy_streamed_media_window_fullscreen_set_video_widget (
    EmpathyStreamedMediaWindowFullscreen *fs,
    GtkWidget *video_widget);
void empathy_streamed_media_window_fullscreen_show_popup (
    EmpathyStreamedMediaWindowFullscreen *fs);

G_END_DECLS

#endif /* #ifndef __EMPATHY_STREAMED_MEDIA_WINDOW_FULLSCREEN_H__*/
