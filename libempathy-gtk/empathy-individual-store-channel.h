/*
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2007-2011 Collabora Ltd.
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
 * Authors: Mikael Hallendal <micke@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 *          Travis Reitter <travis.reitter@collabora.co.uk>
 */

#ifndef __EMPATHY_INDIVIDUAL_STORE_CHANNEL_H__
#define __EMPATHY_INDIVIDUAL_STORE_CHANNEL_H__

#include <gtk/gtk.h>

#include <libempathy-gtk/empathy-individual-store.h>

G_BEGIN_DECLS
#define EMPATHY_TYPE_INDIVIDUAL_STORE_CHANNEL         (empathy_individual_store_channel_get_type ())
#define EMPATHY_INDIVIDUAL_STORE_CHANNEL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_INDIVIDUAL_STORE_CHANNEL, EmpathyIndividualStoreChannel))
#define EMPATHY_INDIVIDUAL_STORE_CHANNEL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_INDIVIDUAL_STORE_CHANNEL, EmpathyIndividualStoreChannelClass))
#define EMPATHY_IS_INDIVIDUAL_STORE_CHANNEL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_INDIVIDUAL_STORE_CHANNEL))
#define EMPATHY_IS_INDIVIDUAL_STORE_CHANNEL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_INDIVIDUAL_STORE_CHANNEL))
#define EMPATHY_INDIVIDUAL_STORE_CHANNEL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_INDIVIDUAL_STORE_CHANNEL, EmpathyIndividualStoreChannelClass))

typedef struct _EmpathyIndividualStoreChannel EmpathyIndividualStoreChannel;
typedef struct _EmpathyIndividualStoreChannelClass EmpathyIndividualStoreChannelClass;
typedef struct _EmpathyIndividualStoreChannelPriv EmpathyIndividualStoreChannelPriv;

struct _EmpathyIndividualStoreChannel
{
  EmpathyIndividualStore parent;
  EmpathyIndividualStoreChannelPriv *priv;
};

struct _EmpathyIndividualStoreChannelClass
{
  EmpathyIndividualStoreClass parent_class;
};

GType empathy_individual_store_channel_get_type (void) G_GNUC_CONST;

EmpathyIndividualStoreChannel * empathy_individual_store_channel_new (
    TpChannel *channel);

TpChannel * empathy_individual_store_channel_get_channel (
    EmpathyIndividualStoreChannel *store);

G_END_DECLS
#endif /* __EMPATHY_INDIVIDUAL_STORE_CHANNEL_H__ */
