/*
 * Copyright (C) 2011 Collabora Ltd.
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
 * Authors: Emilio Pozuelo Monfort <emilio.pozuelo@collabora.co.uk>
 */

#ifndef __EMPATHY_CALL_OBSERVER_H__
#define __EMPATHY_CALL_OBSERVER_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_CALL_OBSERVER         (empathy_call_observer_get_type ())
#define EMPATHY_CALL_OBSERVER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_CALL_OBSERVER, EmpathyCallObserver))
#define EMPATHY_CALL_OBSERVER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_CALL_OBSERVER, EmpathyCallObserverClass))
#define EMPATHY_IS_CALL_OBSERVER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_CALL_OBSERVER))
#define EMPATHY_IS_CALL_OBSERVER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_CALL_OBSERVER))
#define EMPATHY_CALL_OBSERVER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_CALL_OBSERVER, EmpathyCallObserverClass))

typedef struct _EmpathyCallObserver      EmpathyCallObserver;
typedef struct _EmpathyCallObserverPriv  EmpathyCallObserverPriv;
typedef struct _EmpathyCallObserverClass EmpathyCallObserverClass;

struct _EmpathyCallObserver {
  GObject parent;
  EmpathyCallObserverPriv *priv;
};

struct _EmpathyCallObserverClass {
  GObjectClass parent_class;
};

GType empathy_call_observer_get_type (void) G_GNUC_CONST;
EmpathyCallObserver *empathy_call_observer_dup_singleton (void);

G_END_DECLS

#endif /* __EMPATHY_CALL_OBSERVER_H__ */
