/*
 * Copyright (C) 2010 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 *
 * Authors: Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
 */

#ifndef __EMPATHY_CLIENT_FACTORY_H__
#define __EMPATHY_CLIENT_FACTORY_H__

#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_CLIENT_FACTORY         (empathy_client_factory_get_type ())
#define EMPATHY_CLIENT_FACTORY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_CLIENT_FACTORY, EmpathyClientFactory))
#define EMPATHY_CLIENT_FACTORY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_CLIENT_FACTORY, EmpathyClientFactoryClass))
#define EMPATHY_IS_CLIENT_FACTORY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_CLIENT_FACTORY))
#define EMPATHY_IS_CLIENT_FACTORY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_CLIENT_FACTORY))
#define EMPATHY_CLIENT_FACTORY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_CLIENT_FACTORY, EmpathyClientFactoryClass))

typedef struct _EmpathyClientFactory EmpathyClientFactory;
typedef struct _EmpathyClientFactoryClass EmpathyClientFactoryClass;
typedef struct _EmpathyClientFactoryPrivate EmpathyClientFactoryPrivate;

struct _EmpathyClientFactory
{
  TpAutomaticClientFactory parent;
  EmpathyClientFactoryPrivate *priv;
};

struct _EmpathyClientFactoryClass
{
  TpAutomaticClientFactoryClass parent_class;
};

GType empathy_client_factory_get_type (void) G_GNUC_CONST;

EmpathyClientFactory * empathy_client_factory_dup (void);

G_END_DECLS
#endif /* __EMPATHY_CLIENT_FACTORY_H__ */
