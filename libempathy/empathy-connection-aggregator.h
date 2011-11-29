/*
 * empathy-connection-aggregator.h - Header for EmpathyConnectionAggregator
 * Copyright (C) 2010 Collabora Ltd.
 * @author Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
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

#ifndef __EMPATHY_CONNECTION_AGGREGATOR_H__
#define __EMPATHY_CONNECTION_AGGREGATOR_H__

#include <glib-object.h>

#include <telepathy-glib/base-client.h>

G_BEGIN_DECLS

typedef struct _EmpathyConnectionAggregator EmpathyConnectionAggregator;
typedef struct _EmpathyConnectionAggregatorClass EmpathyConnectionAggregatorClass;
typedef struct _EmpathyConnectionAggregatorPriv EmpathyConnectionAggregatorPriv;

struct _EmpathyConnectionAggregatorClass {
    GObjectClass parent_class;
};

struct _EmpathyConnectionAggregator {
    GObject parent;
    EmpathyConnectionAggregatorPriv *priv;
};

GType empathy_connection_aggregator_get_type (void);

/* TYPE MACROS */
#define EMPATHY_TYPE_CONNECTION_AGGREGATOR \
  (empathy_connection_aggregator_get_type ())
#define EMPATHY_CONNECTION_AGGREGATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EMPATHY_TYPE_CONNECTION_AGGREGATOR, \
    EmpathyConnectionAggregator))
#define EMPATHY_CONNECTION_AGGREGATOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EMPATHY_TYPE_CONNECTION_AGGREGATOR, \
    EmpathyConnectionAggregatorClass))
#define EMPATHY_IS_CONNECTION_AGGREGATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EMPATHY_TYPE_CONNECTION_AGGREGATOR))
#define EMPATHY_IS_CONNECTION_AGGREGATOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EMPATHY_TYPE_CONNECTION_AGGREGATOR))
#define EMPATHY_CONNECTION_AGGREGATOR_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_CONNECTION_AGGREGATOR, \
    EmpathyConnectionAggregatorClass))

EmpathyConnectionAggregator * empathy_connection_aggregator_dup_singleton (void);

GList * empathy_connection_aggregator_get_all_groups (
    EmpathyConnectionAggregator *self);

GPtrArray * empathy_connection_aggregator_dup_all_contacts (
    EmpathyConnectionAggregator *self);

G_END_DECLS

#endif /* #ifndef __EMPATHY_CONNECTION_AGGREGATOR_H__*/
