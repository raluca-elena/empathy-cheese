/*
 * empathy-tls-certificate.h - Header for EmpathyTLSCertificate
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

#ifndef __EMPATHY_TLS_CERTIFICATE_H__
#define __EMPATHY_TLS_CERTIFICATE_H__

#include <glib-object.h>
#include <gio/gio.h>

#include <telepathy-glib/proxy-subclass.h>

#include <extensions/extensions.h>

G_BEGIN_DECLS

typedef struct _EmpathyTLSCertificate EmpathyTLSCertificate;
typedef struct _EmpathyTLSCertificateClass EmpathyTLSCertificateClass;

struct _EmpathyTLSCertificateClass {
    TpProxyClass parent_class;
};

struct _EmpathyTLSCertificate {
    TpProxy parent;
    gpointer priv;
};

GType empathy_tls_certificate_get_type (void);

#define EMPATHY_TYPE_TLS_CERTIFICATE \
  (empathy_tls_certificate_get_type ())
#define EMPATHY_TLS_CERTIFICATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EMPATHY_TYPE_TLS_CERTIFICATE, \
    EmpathyTLSCertificate))
#define EMPATHY_TLS_CERTIFICATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EMPATHY_TYPE_TLS_CERTIFICATE, \
  EmpathyTLSCertificateClass))
#define EMPATHY_IS_TLS_CERTIFICATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EMPATHY_TYPE_TLS_CERTIFICATE))
#define EMPATHY_IS_TLS_CERTIFICATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EMPATHY_TYPE_TLS_CERTIFICATE))
#define EMPATHY_TLS_CERTIFICATE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_TLS_CERTIFICATE, \
  EmpathyTLSCertificateClass))

EmpathyTLSCertificate * empathy_tls_certificate_new (TpDBusDaemon *dbus,
    const gchar *bus_name,
    const gchar *object_path,
    GError **error);

void empathy_tls_certificate_prepare_async (EmpathyTLSCertificate *self,
    GAsyncReadyCallback callback,
    gpointer user_data);
gboolean empathy_tls_certificate_prepare_finish (EmpathyTLSCertificate *self,
    GAsyncResult *result,
    GError **error);

void empathy_tls_certificate_accept_async (EmpathyTLSCertificate *self,
    GAsyncReadyCallback callback,
    gpointer user_data);
gboolean empathy_tls_certificate_accept_finish (EmpathyTLSCertificate *self,
    GAsyncResult *result,
    GError **error);

void empathy_tls_certificate_reject_async (EmpathyTLSCertificate *self,
    EmpTLSCertificateRejectReason reason,
    GHashTable *details,
    GAsyncReadyCallback callback,
    gpointer user_data);
gboolean empathy_tls_certificate_reject_finish (EmpathyTLSCertificate *self,
    GAsyncResult *result,
    GError **error);

G_END_DECLS

#endif /* #ifndef __EMPATHY_TLS_CERTIFICATE_H__*/
