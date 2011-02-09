/*
 * empathy-streamed-media-factory.h - Header for EmpathyStreamedMediaFactory
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

#ifndef __EMPATHY_STREAMED_MEDIA_FACTORY_H__
#define __EMPATHY_STREAMED_MEDIA_FACTORY_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _EmpathyStreamedMediaFactory EmpathyStreamedMediaFactory;
typedef struct _EmpathyStreamedMediaFactoryClass EmpathyStreamedMediaFactoryClass;

struct _EmpathyStreamedMediaFactoryClass {
    GObjectClass parent_class;
};

struct _EmpathyStreamedMediaFactory {
    GObject parent;
    gpointer priv;
};

GType empathy_streamed_media_factory_get_type (void);

/* TYPE MACROS */
#define EMPATHY_TYPE_STREAMED_MEDIA_FACTORY \
  (empathy_streamed_media_factory_get_type ())
#define EMPATHY_STREAMED_MEDIA_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EMPATHY_TYPE_STREAMED_MEDIA_FACTORY, \
    EmpathyStreamedMediaFactory))
#define EMPATHY_STREAMED_MEDIA_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EMPATHY_TYPE_STREAMED_MEDIA_FACTORY, \
    EmpathyStreamedMediaFactoryClass))
#define EMPATHY_IS_STREAMED_MEDIA_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EMPATHY_TYPE_STREAMED_MEDIA_FACTORY))
#define EMPATHY_IS_STREAMED_MEDIA_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EMPATHY_TYPE_STREAMED_MEDIA_FACTORY))
#define EMPATHY_STREAMED_MEDIA_FACTORY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_STREAMED_MEDIA_FACTORY, \
    EmpathyStreamedMediaFactoryClass))


EmpathyStreamedMediaFactory *empathy_streamed_media_factory_initialise (void);

EmpathyStreamedMediaFactory *empathy_streamed_media_factory_get (void);

gboolean empathy_streamed_media_factory_register (EmpathyStreamedMediaFactory *self,
    GError **error);

G_END_DECLS

#endif /* #ifndef __EMPATHY_STREAMED_MEDIA_FACTORY_H__*/
