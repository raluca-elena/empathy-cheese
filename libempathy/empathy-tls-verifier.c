/*
 * empathy-tls-verifier.c - Source for EmpathyTLSVerifier
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

#include <config.h>

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include <telepathy-glib/util.h>

#include "empathy-tls-verifier.h"

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

static const gchar* system_ca_paths[] = {
  GTLS_SYSTEM_CA_FILE,
  NULL,
};

typedef struct {
  GPtrArray *cert_chain;

  GPtrArray *trusted_ca_list;
  GPtrArray *trusted_crl_list;

  EmpathyTLSCertificate *certificate;
  gchar *hostname;

  GSimpleAsyncResult *verify_result;
  GHashTable *details;

  gboolean dispose_run;
} EmpathyTLSVerifierPriv;

static gnutls_x509_crt_t *
ptr_array_to_x509_crt_list (GPtrArray *chain)
{
  gnutls_x509_crt_t *retval;
  gint idx;

  retval = g_malloc0 (sizeof (gnutls_x509_crt_t) * chain->len);

  for (idx = 0; idx < (gint) chain->len; idx++)
    retval[idx] = g_ptr_array_index (chain, idx);

  return retval;
}

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
verify_last_certificate (EmpathyTLSVerifier *self,
    gnutls_x509_crt_t cert,
    EmpTLSCertificateRejectReason *reason)
{
  guint verify_output;
  gint res;
  gnutls_x509_crt_t *trusted_ca_list;
  EmpathyTLSVerifierPriv *priv = GET_PRIV (self);

  if (priv->trusted_ca_list->len > 0)
    {
      trusted_ca_list = ptr_array_to_x509_crt_list (priv->trusted_ca_list);
      res = gnutls_x509_crt_verify (cert, trusted_ca_list,
          priv->trusted_ca_list->len, 0, &verify_output);

      DEBUG ("Checking last certificate %p against trusted CAs, output %u",
          cert, verify_output);

      g_free (trusted_ca_list);
    }
  else
    {
      /* check it against itself to see if it's structurally valid */
      res = gnutls_x509_crt_verify (cert, &cert, 1, 0, &verify_output);

      DEBUG ("Checking last certificate %p against itself, output %u", cert,
          verify_output);

      /* if it's valid, return the SelfSigned error, so that we can add it
       * later to our trusted CAs whitelist.
       */
      if (res == GNUTLS_E_SUCCESS)
        {
          *reason = EMP_TLS_CERTIFICATE_REJECT_REASON_SELF_SIGNED;
          return FALSE;
        }
    }

  return verification_output_to_reason (res, verify_output, reason);
}

static gboolean
verify_certificate (EmpathyTLSVerifier *self,
    gnutls_x509_crt_t cert,
    gnutls_x509_crt_t issuer,
    EmpTLSCertificateRejectReason *reason)
{
  guint verify_output;
  gint res;

  res = gnutls_x509_crt_verify (cert, &issuer, 1, 0, &verify_output);

  DEBUG ("Verifying %p against %p, output %u", cert, issuer, verify_output);

  return verification_output_to_reason (res, verify_output, reason);
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
real_start_verification (EmpathyTLSVerifier *self)
{
  gnutls_x509_crt_t first_cert, last_cert;
  gint idx;
  gboolean res = FALSE;
  gint num_certs;
  EmpTLSCertificateRejectReason reason =
    EMP_TLS_CERTIFICATE_REJECT_REASON_UNKNOWN;
  EmpathyTLSVerifierPriv *priv = GET_PRIV (self);

  DEBUG ("Starting verification");

  /* check if the certificate matches the hostname first. */
  first_cert = g_ptr_array_index (priv->cert_chain, 0);
  if (gnutls_x509_crt_check_hostname (first_cert, priv->hostname) == 0)
    {
      gchar *certified_hostname;

      reason = EMP_TLS_CERTIFICATE_REJECT_REASON_HOSTNAME_MISMATCH;
      certified_hostname = empathy_get_x509_certificate_hostname (first_cert);
      tp_asv_set_string (priv->details,
          "expected-hostname", priv->hostname);
      tp_asv_set_string (priv->details,
          "certificate-hostname", certified_hostname);

      DEBUG ("Hostname mismatch: got %s but expected %s",
          certified_hostname, priv->hostname);

      g_free (certified_hostname);
      goto out;
    }

  DEBUG ("Hostname matched");

  num_certs = priv->cert_chain->len;

  if (priv->trusted_ca_list->len > 0)
    {
      /* if the last certificate is self-signed, and we have a list of
       * trusted CAs, ignore it, as we want to check the chain against our
       * trusted CAs list first.
       * if we have only one certificate in the chain, don't ignore it though,
       * as it's the CA certificate itself.
       */
      last_cert = g_ptr_array_index (priv->cert_chain, num_certs - 1);

      if (gnutls_x509_crt_check_issuer (last_cert, last_cert) > 0 &&
          num_certs > 1)
        num_certs--;
    }

  for (idx = 1; idx < num_certs; idx++)
    {
      res = verify_certificate (self,
          g_ptr_array_index (priv->cert_chain, idx -1),
          g_ptr_array_index (priv->cert_chain, idx),
          &reason);

      DEBUG ("Certificate verification %d gave result %d with reason %u", idx,
          res, reason);

      if (!res)
        {
          abort_verification (self, reason);
          return;
        }
    }

  res = verify_last_certificate (self,
      g_ptr_array_index (priv->cert_chain, num_certs - 1),
      &reason);

  DEBUG ("Last verification gave result %d with reason %u", res, reason);

 out:
  if (!res)
    {
      abort_verification (self, reason);
      return;
    }

  complete_verification (self);
}

static gboolean
start_verification (gpointer user_data)
{
  EmpathyTLSVerifier *self = user_data;

  real_start_verification (self);

  return FALSE;
}

static void
build_gnutls_cert_list (EmpathyTLSVerifier *self)
{
  guint num_certs;
  guint idx;
  GPtrArray *certificate_data = NULL;
  EmpathyTLSVerifierPriv *priv = GET_PRIV (self);

  g_object_get (priv->certificate,
      "cert-data", &certificate_data,
      NULL);
  num_certs = certificate_data->len;

  priv->cert_chain = g_ptr_array_new_with_free_func (
      (GDestroyNotify) gnutls_x509_crt_deinit);

  for (idx = 0; idx < num_certs; idx++)
    {
      gnutls_x509_crt_t cert;
      GArray *one_cert;
      gnutls_datum_t datum = { NULL, 0 };

      one_cert = g_ptr_array_index (certificate_data, idx);
      datum.data = (guchar *) one_cert->data;
      datum.size = one_cert->len;

      gnutls_x509_crt_init (&cert);
      gnutls_x509_crt_import (cert, &datum, GNUTLS_X509_FMT_DER);

      g_ptr_array_add (priv->cert_chain, cert);
    }
}

static gint
get_number_and_type_of_certificates (gnutls_datum_t *datum,
    gnutls_x509_crt_fmt_t *format)
{
  gnutls_x509_crt_t fake;
  guint retval = 1;
  gint res;

  res = gnutls_x509_crt_list_import (&fake, &retval, datum,
      GNUTLS_X509_FMT_PEM, GNUTLS_X509_CRT_LIST_IMPORT_FAIL_IF_EXCEED);

  if (res == GNUTLS_E_SHORT_MEMORY_BUFFER || res > 0)
    {
      DEBUG ("Found PEM, with %u certificates", retval);
      *format = GNUTLS_X509_FMT_PEM;
      return retval;
    }

  /* try DER */
  res = gnutls_x509_crt_list_import (&fake, &retval, datum,
      GNUTLS_X509_FMT_DER, 0);

  if (res > 0)
    {
      *format = GNUTLS_X509_FMT_DER;
      return retval;
    }

  return res;
}

static gboolean
build_gnutls_ca_and_crl_lists (GIOSchedulerJob *job,
    GCancellable *cancellable,
    gpointer user_data)
{
  gint idx;
  gchar *user_certs_dir;
  GDir *dir;
  GError *error = NULL;
  EmpathyTLSVerifier *self = user_data;
  EmpathyTLSVerifierPriv *priv = GET_PRIV (self);

  priv->trusted_ca_list = g_ptr_array_new_with_free_func
    ((GDestroyNotify) gnutls_x509_crt_deinit);

  for (idx = 0; idx < (gint) G_N_ELEMENTS (system_ca_paths) - 1; idx++)
    {
      const gchar *path;
      gchar *contents = NULL;
      gsize length = 0;
      gint res, n_certs;
      gnutls_x509_crt_t *cert_list;
      gnutls_datum_t datum = { NULL, 0 };
      gnutls_x509_crt_fmt_t format = 0;

      path = system_ca_paths[idx];
      g_file_get_contents (path, &contents, &length, &error);

      if (error != NULL)
        {
          DEBUG ("Unable to read system CAs from path %s: %s", path,
              error->message);
          g_clear_error (&error);
          continue;
        }

      datum.data = (guchar *) contents;
      datum.size = length;
      n_certs = get_number_and_type_of_certificates (&datum, &format);

      if (n_certs < 0)
        {
          DEBUG ("Unable to parse the system CAs from path %s: GnuTLS "
              "returned error %d", path, n_certs);

          g_free (contents);
          continue;
        }

      cert_list = g_malloc0 (sizeof (gnutls_x509_crt_t) * n_certs);
      res = gnutls_x509_crt_list_import (cert_list, (guint *) &n_certs, &datum,
          format, 0);

      if (res < 0)
        {
          DEBUG ("Unable to import system CAs from path %s; "
              "GnuTLS returned error %d", path, res);

          g_free (contents);
          continue;
        }

      DEBUG ("Successfully imported %d system CA certificates from path %s",
          n_certs, path);

      /* append the newly created cert structutes into the global GPtrArray */
      for (idx = 0; idx < n_certs; idx++)
        g_ptr_array_add (priv->trusted_ca_list, cert_list[idx]);

      g_free (contents);
      g_free (cert_list);
    }

  /* user certs */
  user_certs_dir = g_build_filename (g_get_user_config_dir (),
      "telepathy", "certs", NULL);
  dir = g_dir_open (user_certs_dir, 0, &error);

  if (error != NULL)
    {
      DEBUG ("Can't open the user certs dir at %s: %s", user_certs_dir,
          error->message);

      g_error_free (error);
    }
  else
    {
      const gchar *cert_name;

      while ((cert_name = g_dir_read_name (dir)) != NULL)
        {
          gchar *contents = NULL, *cert_path = NULL;
          gsize length = 0;
          gint res;
          gnutls_datum_t datum = { NULL, 0 };
          gnutls_x509_crt_t cert;

          cert_path = g_build_filename (user_certs_dir, cert_name, NULL);

          g_file_get_contents (cert_path, &contents, &length, &error);

          if (error != NULL)
            {
              DEBUG ("Can't open the certificate file at path %s: %s",
                  cert_path, error->message);

              g_clear_error (&error);
              g_free (cert_path);
              continue;
            }

          datum.data = (guchar *) contents;
          datum.size = length;

          gnutls_x509_crt_init (&cert);
          res = gnutls_x509_crt_import (cert, &datum, GNUTLS_X509_FMT_PEM);

          if (res != GNUTLS_E_SUCCESS)
            {
              DEBUG ("Can't import the certificate at path %s: "
                  "GnuTLS returned %d", cert_path, res);
            }
          else
            {
              g_ptr_array_add (priv->trusted_ca_list, cert);
            }

          g_free (contents);
          g_free (cert_path);
        }

      g_dir_close (dir);
    }

  g_free (user_certs_dir);

  /* TODO: do the CRL too */

  g_io_scheduler_job_send_to_mainloop_async (job,
      start_verification, self, NULL);

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

  tp_clear_pointer (&priv->trusted_ca_list, g_ptr_array_unref);
  tp_clear_pointer (&priv->cert_chain, g_ptr_array_unref);
  tp_clear_boxed (G_TYPE_HASH_TABLE, &priv->details);
  g_free (priv->hostname);

  G_OBJECT_CLASS (empathy_tls_verifier_parent_class)->finalize (object);
}

static void
empathy_tls_verifier_constructed (GObject *object)
{
  EmpathyTLSVerifier *self = EMPATHY_TLS_VERIFIER (object);

  build_gnutls_cert_list (self);

  if (G_OBJECT_CLASS (empathy_tls_verifier_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (empathy_tls_verifier_parent_class)->constructed (object);
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
  oclass->constructed = empathy_tls_verifier_constructed;

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

  g_io_scheduler_push_job (build_gnutls_ca_and_crl_lists,
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
