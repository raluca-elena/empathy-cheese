/*
 * empathy-tls-verifier.c - Source for EmpathyTLSVerifier
 * Copyright (C) 2010 Collabora Ltd.
 * @author Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 * @author Stef Walter <stefw@collabora.co.uk>
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

#include <config.h>

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include <telepathy-glib/util.h>

#include "empathy-tls-verifier.h"

#include <gcr/gcr-simple-certificate.h>
#include <gcr/gcr-trust.h>

#define DEBUG_FLAG EMPATHY_DEBUG_TLS
#include "empathy-debug.h"
#include "empathy-utils.h"

G_DEFINE_TYPE (EmpathyTLSVerifier, empathy_tls_verifier,
    G_TYPE_OBJECT)

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyTLSVerifier);

enum {
  PROP_TLS_CERTIFICATE = 1,
  PROP_HOSTNAME,

  LAST_PROPERTY,
};

typedef struct {
  EmpathyTLSCertificate *certificate;
  gchar *hostname;

  GSimpleAsyncResult *verify_result;
  GHashTable *details;

  gboolean dispose_run;
} EmpathyTLSVerifierPriv;

static gboolean
verification_output_to_reason (gint res,
    guint verify_output,
    EmpTLSCertificateRejectReason *reason)
{
  gboolean retval = TRUE;

  g_assert (reason != NULL);

  if (res != GNUTLS_E_SUCCESS)
    {
      retval = FALSE;

      /* the certificate is not structurally valid */
      switch (res)
        {
        case GNUTLS_E_INSUFFICIENT_CREDENTIALS:
          *reason = EMP_TLS_CERTIFICATE_REJECT_REASON_UNTRUSTED;
          break;
        case GNUTLS_E_CONSTRAINT_ERROR:
          *reason = EMP_TLS_CERTIFICATE_REJECT_REASON_LIMIT_EXCEEDED;
          break;
        default:
          *reason = EMP_TLS_CERTIFICATE_REJECT_REASON_UNKNOWN;
          break;
        }

      goto out;
    }

  /* the certificate is structurally valid, check for other errors. */
  if (verify_output & GNUTLS_CERT_INVALID)
    {
      retval = FALSE;

      if (verify_output & GNUTLS_CERT_SIGNER_NOT_FOUND)
        *reason = EMP_TLS_CERTIFICATE_REJECT_REASON_SELF_SIGNED;
      else if (verify_output & GNUTLS_CERT_SIGNER_NOT_CA)
        *reason = EMP_TLS_CERTIFICATE_REJECT_REASON_UNTRUSTED;
      else if (verify_output & GNUTLS_CERT_INSECURE_ALGORITHM)
        *reason = EMP_TLS_CERTIFICATE_REJECT_REASON_INSECURE;
      else if (verify_output & GNUTLS_CERT_NOT_ACTIVATED)
        *reason = EMP_TLS_CERTIFICATE_REJECT_REASON_NOT_ACTIVATED;
      else if (verify_output & GNUTLS_CERT_EXPIRED)
        *reason = EMP_TLS_CERTIFICATE_REJECT_REASON_EXPIRED;
      else
        *reason = EMP_TLS_CERTIFICATE_REJECT_REASON_UNKNOWN;

      goto out;
    }

 out:
  return retval;
}

static gboolean
check_is_certificate_exception (EmpathyTLSVerifier *self,
        gconstpointer data, gsize n_data)
{
  GcrCertificate *cert;
  GError *error = NULL;
  gboolean ret;
  EmpathyTLSVerifierPriv *priv = GET_PRIV (self);

  cert = gcr_simple_certificate_new_static (data, n_data);
  ret = gcr_trust_is_certificate_exception (cert, GCR_PURPOSE_CLIENT_AUTH,
          priv->hostname, NULL, &error);
  g_object_unref (cert);

  if (!ret && error) {
      DEBUG ("Can't lookup certificate exception for %s: %s", priv->hostname,
              error->message);
      g_clear_error (&error);
  }

  return ret;
}

static gboolean
check_is_certificate_anchor (EmpathyTLSVerifier *self,
        gconstpointer data, gsize n_data)
{
  GcrCertificate *cert;
  GError *error = NULL;
  gboolean ret;

  cert = gcr_simple_certificate_new_static (data, n_data);
  ret = gcr_trust_is_certificate_anchor (cert, GCR_PURPOSE_CLIENT_AUTH,
          NULL, &error);
  g_object_unref (cert);

  if (!ret && error) {
      DEBUG ("Can't lookup certificate anchor: %s", error->message);
      g_clear_error (&error);
  }

  return ret;
}

static gnutls_x509_crt_t
convert_cert_to_gnutls (GArray *cert_data)
{
  gnutls_x509_crt_t cert;
  gnutls_datum_t datum = { (unsigned char*)cert_data->data, cert_data->len };

  gnutls_x509_crt_init (&cert);
  gnutls_x509_crt_import (cert, &datum, GNUTLS_X509_FMT_DER);

  return cert;
}

static void
complete_verification (EmpathyTLSVerifier *self)
{
  EmpathyTLSVerifierPriv *priv = GET_PRIV (self);

  DEBUG ("Verification successful, completing...");

  g_simple_async_result_complete_in_idle (priv->verify_result);

  tp_clear_object (&priv->verify_result);
}

static void
abort_verification (EmpathyTLSVerifier *self,
    EmpTLSCertificateRejectReason reason)
{
  EmpathyTLSVerifierPriv *priv = GET_PRIV (self);

  DEBUG ("Verification error %u, aborting...", reason);

  g_simple_async_result_set_error (priv->verify_result,
      G_IO_ERROR, reason, "TLS verification failed with reason %u",
      reason);
  g_simple_async_result_complete_in_idle (priv->verify_result);

  tp_clear_object (&priv->verify_result);
}

static void
perform_verification (EmpathyTLSVerifier *self)
{
  gnutls_x509_crt_t cert, anchor;
  gboolean ret = FALSE;
  EmpTLSCertificateRejectReason reason =
    EMP_TLS_CERTIFICATE_REJECT_REASON_UNKNOWN;
  gsize idx;
  GPtrArray *certs = NULL;
  GArray *cert_data;
  GPtrArray *cert_chain;
  gint res;
  guint verify_output;
  EmpathyTLSVerifierPriv *priv = GET_PRIV (self);

  DEBUG ("Starting verification");

  g_object_get (priv->certificate, "cert-data", &certs, NULL);
  cert_chain = g_ptr_array_new_with_free_func
          ((GDestroyNotify)gnutls_x509_crt_deinit);

  /*
   * If the first certificate is an exception then we completely
   * ignore the rest of the verification process.
   */
  cert_data = g_ptr_array_index (certs, 0);
  if (check_is_certificate_exception (self, cert_data->data, cert_data->len)) {
      DEBUG ("Found certificate exception for %s", priv->hostname);
      complete_verification (self);
      goto out;
  }

  cert = convert_cert_to_gnutls (cert_data);
  g_ptr_array_add (cert_chain, cert);

  /*
   * Find out which of our certificates is the anchor. Note that we
   * don't allow the leaf certificate on the tree to be an anchor.
   * Also build up the certificate chain. But only up to our anchor.
   */
  anchor = NULL;
  for (idx = 1; idx < certs->len; idx++)
    {
      cert_data = g_ptr_array_index (certs, idx);

      /* Add this to the chain */
      cert = convert_cert_to_gnutls (cert_data);
      g_ptr_array_add (cert_chain, cert);

      /* Stop the chain at the first anchor */
      if (check_is_certificate_anchor (self, cert_data->data,
              cert_data->len)) {
          anchor = cert;
          break;
      }
    }

  verify_output = 0;
  res = gnutls_x509_crt_list_verify
          ((const gnutls_x509_crt_t*)cert_chain->pdata,
           cert_chain->len, anchor ? &anchor : NULL, anchor ? 1 : 1,
           NULL, 0, 0, &verify_output);
  ret = verification_output_to_reason (res, verify_output, &reason);

  DEBUG ("Certificate verification gave result %d with reason %u", ret,
          reason);

  if (!ret) {
      g_ptr_array_free (cert_chain, TRUE);
      abort_verification (self, reason);
      goto out;
  }

  /* now check if the certificate matches the hostname first. */
  cert = g_ptr_array_index (cert_chain, 0);
  if (gnutls_x509_crt_check_hostname (cert, priv->hostname) == 0)
    {
      gchar *certified_hostname;

      certified_hostname = empathy_get_x509_certificate_hostname (cert);
      tp_asv_set_string (priv->details,
          "expected-hostname", priv->hostname);
      tp_asv_set_string (priv->details,
          "certificate-hostname", certified_hostname);

      DEBUG ("Hostname mismatch: got %s but expected %s",
          certified_hostname, priv->hostname);

      g_free (certified_hostname);
      abort_verification (self,
              EMP_TLS_CERTIFICATE_REJECT_REASON_HOSTNAME_MISMATCH);
      goto out;
    }

  DEBUG ("Hostname matched");

  /* TODO: And here is where we check negative trust (ie: revocation) */

 out:
  g_ptr_array_free (cert_chain, TRUE);
}

static gboolean
perform_verification_cb (gpointer user_data)
{
  EmpathyTLSVerifier *self = user_data;

  perform_verification (self);

  return FALSE;
}

static gboolean
start_verification (GIOSchedulerJob *job,
    GCancellable *cancellable,
    gpointer user_data)
{
  EmpathyTLSVerifier *self = user_data;

  g_io_scheduler_job_send_to_mainloop_async (job,
      perform_verification_cb, self, NULL);

  return FALSE;
}

static void
empathy_tls_verifier_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyTLSVerifierPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_TLS_CERTIFICATE:
      g_value_set_object (value, priv->certificate);
      break;
    case PROP_HOSTNAME:
      g_value_set_string (value, priv->hostname);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_tls_verifier_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyTLSVerifierPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_TLS_CERTIFICATE:
      priv->certificate = g_value_dup_object (value);
      break;
    case PROP_HOSTNAME:
      priv->hostname = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_tls_verifier_dispose (GObject *object)
{
  EmpathyTLSVerifierPriv *priv = GET_PRIV (object);

  if (priv->dispose_run)
    return;

  priv->dispose_run = TRUE;

  tp_clear_object (&priv->certificate);

  G_OBJECT_CLASS (empathy_tls_verifier_parent_class)->dispose (object);
}

static void
empathy_tls_verifier_finalize (GObject *object)
{
  EmpathyTLSVerifierPriv *priv = GET_PRIV (object);

  DEBUG ("%p", object);

  tp_clear_boxed (G_TYPE_HASH_TABLE, &priv->details);
  g_free (priv->hostname);

  G_OBJECT_CLASS (empathy_tls_verifier_parent_class)->finalize (object);
}

static void
empathy_tls_verifier_init (EmpathyTLSVerifier *self)
{
  EmpathyTLSVerifierPriv *priv;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_TLS_VERIFIER, EmpathyTLSVerifierPriv);
  priv->details = tp_asv_new (NULL, NULL);
}

static void
empathy_tls_verifier_class_init (EmpathyTLSVerifierClass *klass)
{
  GParamSpec *pspec;
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (EmpathyTLSVerifierPriv));

  oclass->set_property = empathy_tls_verifier_set_property;
  oclass->get_property = empathy_tls_verifier_get_property;
  oclass->finalize = empathy_tls_verifier_finalize;
  oclass->dispose = empathy_tls_verifier_dispose;

  pspec = g_param_spec_object ("certificate", "The EmpathyTLSCertificate",
      "The EmpathyTLSCertificate to be verified.",
      EMPATHY_TYPE_TLS_CERTIFICATE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_TLS_CERTIFICATE, pspec);

  pspec = g_param_spec_string ("hostname", "The hostname",
      "The hostname which should be certified by the certificate.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_HOSTNAME, pspec);
}

EmpathyTLSVerifier *
empathy_tls_verifier_new (EmpathyTLSCertificate *certificate,
    const gchar *hostname)
{
  g_assert (EMPATHY_IS_TLS_CERTIFICATE (certificate));
  g_assert (hostname != NULL);

  return g_object_new (EMPATHY_TYPE_TLS_VERIFIER,
      "certificate", certificate,
      "hostname", hostname,
      NULL);
}

void
empathy_tls_verifier_verify_async (EmpathyTLSVerifier *self,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  EmpathyTLSVerifierPriv *priv = GET_PRIV (self);

  g_return_if_fail (priv->verify_result == NULL);

  priv->verify_result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, NULL);

  g_io_scheduler_push_job (start_verification,
      self, NULL, G_PRIORITY_DEFAULT, NULL);
}

gboolean
empathy_tls_verifier_verify_finish (EmpathyTLSVerifier *self,
    GAsyncResult *res,
    EmpTLSCertificateRejectReason *reason,
    GHashTable **details,
    GError **error)
{
  EmpathyTLSVerifierPriv *priv = GET_PRIV (self);

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res),
          error))
    {
      if (reason != NULL)
        *reason = (*error)->code;

      if (details != NULL)
        {
          *details = tp_asv_new (NULL, NULL);
          tp_g_hash_table_update (*details, priv->details,
              (GBoxedCopyFunc) g_strdup,
              (GBoxedCopyFunc) tp_g_value_slice_dup);
        }

      return FALSE;
    }

  if (reason != NULL)
    *reason = EMP_TLS_CERTIFICATE_REJECT_REASON_UNKNOWN;

  return TRUE;
}
