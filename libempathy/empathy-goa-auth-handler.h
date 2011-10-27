/*
 * empathy-auth-goa.h - Header for Goa SASL authentication
 * Copyright (C) 2011 Collabora Ltd.
 * @author Xavier Claessens <xavier.claessens@collabora.co.uk>
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

#ifndef __EMPATHY_GOA_AUTH_HANDLER_H__
#define __EMPATHY_GOA_AUTH_HANDLER_H__

#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

typedef struct _EmpathyGoaAuthHandler EmpathyGoaAuthHandler;
typedef struct _EmpathyGoaAuthHandlerClass EmpathyGoaAuthHandlerClass;
typedef struct _EmpathyGoaAuthHandlerPriv EmpathyGoaAuthHandlerPriv;

struct _EmpathyGoaAuthHandlerClass {
    GObjectClass parent_class;
};

struct _EmpathyGoaAuthHandler {
    GObject parent;
    EmpathyGoaAuthHandlerPriv *priv;
};

GType empathy_goa_auth_handler_get_type (void);

/* TYPE MACROS */
#define EMPATHY_TYPE_GOA_AUTH_HANDLER \
  (empathy_goa_auth_handler_get_type ())
#define EMPATHY_GOA_AUTH_HANDLER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EMPATHY_TYPE_GOA_AUTH_HANDLER, \
    EmpathyGoaAuthHandler))
#define EMPATHY_GOA_AUTH_HANDLER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EMPATHY_TYPE_GOA_AUTH_HANDLER, \
    EmpathyGoaAuthHandlerClass))
#define EMPATHY_IS_GOA_AUTH_HANDLER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EMPATHY_TYPE_GOA_AUTH_HANDLER))
#define EMPATHY_IS_GOA_AUTH_HANDLER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EMPATHY_TYPE_GOA_AUTH_HANDLER))
#define EMPATHY_GOA_AUTH_HANDLER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_GOA_AUTH_HANDLER, \
    EmpathyGoaAuthHandlerClass))

EmpathyGoaAuthHandler *empathy_goa_auth_handler_new (void);

void empathy_goa_auth_handler_start (EmpathyGoaAuthHandler *self,
    TpChannel *channel,
    TpAccount *account);

gboolean empathy_goa_auth_handler_supports (EmpathyGoaAuthHandler *self,
    TpChannel *channel,
    TpAccount *account);

G_END_DECLS

#endif /* #ifndef __EMPATHY_GOA_AUTH_HANDLER_H__*/
