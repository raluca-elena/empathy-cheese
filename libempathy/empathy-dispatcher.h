/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
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
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 */

#ifndef __EMPATHY_DISPATCHER_H__
#define __EMPATHY_DISPATCHER_H__

#include <glib.h>
#include <gio/gio.h>

#include <telepathy-glib/channel.h>

#include "empathy-contact.h"

G_BEGIN_DECLS

/* Requesting 1 to 1 text channels */
void empathy_dispatcher_chat_with_contact_id (TpAccount *account,
  const gchar *contact_id,
  gint64 timestamp);

void  empathy_dispatcher_chat_with_contact (EmpathyContact *contact,
  gint64 timestamp);

/* Request a muc channel */
void empathy_dispatcher_join_muc (TpAccount *account,
  const gchar *roomname,
  gint64 timestamp);

G_END_DECLS

#endif /* __EMPATHY_DISPATCHER_H__ */
