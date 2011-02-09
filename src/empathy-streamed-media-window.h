/*
 * empathy-streamed-media-window.h - Header for EmpathyStreamedMediaWindow
 * Copyright (C) 2008 Collabora Ltd.
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

#ifndef __EMPATHY_STREAMED_MEDIA_WINDOW_H__
#define __EMPATHY_STREAMED_MEDIA_WINDOW_H__

#include <glib-object.h>
#include <gtk/gtk.h>

#include "empathy-streamed-media-handler.h"

G_BEGIN_DECLS

typedef struct _EmpathyStreamedMediaWindow EmpathyStreamedMediaWindow;
typedef struct _EmpathyStreamedMediaWindowClass EmpathyStreamedMediaWindowClass;

struct _EmpathyStreamedMediaWindowClass {
    GtkWindowClass parent_class;
};

struct _EmpathyStreamedMediaWindow {
    GtkWindow parent;
};

GType empathy_streamed_media_window_get_type (void);

/* TYPE MACROS */
#define EMPATHY_TYPE_STREAMED_MEDIA_WINDOW \
  (empathy_streamed_media_window_get_type ())
#define EMPATHY_STREAMED_MEDIA_WINDOW(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EMPATHY_TYPE_STREAMED_MEDIA_WINDOW, \
    EmpathyStreamedMediaWindow))
#define EMPATHY_STREAMED_MEDIA_WINDOW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EMPATHY_TYPE_STREAMED_MEDIA_WINDOW, \
    EmpathyStreamedMediaWindowClass))
#define EMPATHY_IS_STREAMED_MEDIA_WINDOW(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EMPATHY_TYPE_STREAMED_MEDIA_WINDOW))
#define EMPATHY_IS_STREAMED_MEDIA_WINDOW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EMPATHY_TYPE_STREAMED_MEDIA_WINDOW))
#define EMPATHY_STREAMED_MEDIA_WINDOW_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_STREAMED_MEDIA_WINDOW, \
    EmpathyStreamedMediaWindowClass))

EmpathyStreamedMediaWindow *empathy_streamed_media_window_new (EmpathyStreamedMediaHandler *handler);

G_END_DECLS

#endif /* #ifndef __EMPATHY_STREAMED_MEDIA_WINDOW_H__*/
