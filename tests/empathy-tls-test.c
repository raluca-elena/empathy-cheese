#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libempathy/empathy-tls-certificate.h>
#include <libempathy/empathy-tls-verifier.h>
#include "test-helper.h"

#include <gnutls/gnutls.h>

#include <telepathy-glib/dbus-properties-mixin.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-tls.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/telepathy-glib.h>

#define MOCK_TLS_CERTIFICATE_PATH "/mock/certificate"

/* Forward decl */
GType mock_tls_certificate_get_type (void);

#define MOCK_TLS_CERTIFICATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), mock_tls_certificate_get_type (), \
    MockTLSCertificate))

typedef struct _MockTLSCertificate {
  GObject parent;
  guint state;
  GPtrArray *rejections;
  gchar *cert_type;
  GPtrArray *cert_data;
} MockTLSCertificate;

typedef struct _MockTLSCertificateClass {
  GObjectClass parent;
  TpDBusPropertiesMixinClass dbus_props_class;
} MockTLSCertificateClass;

enum {
  PROP_0,
  PROP_STATE,
  PROP_REJECTIONS,
  PROP_CERTIFICATE_TYPE,
  PROP_CERTIFICATE_CHAIN_DATA
};

static void mock_tls_certificate_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(MockTLSCertificate, mock_tls_certificate, G_TYPE_OBJECT,
        G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_AUTHENTICATION_TLS_CERTIFICATE,
                mock_tls_certificate_iface_init)
        G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
                tp_dbus_properties_mixin_iface_init)
)

static void
mock_tls_certificate_init (MockTLSCertificate *self)
{
  self->state = TP_TLS_CERTIFICATE_STATE_PENDING;
  self->cert_type = g_strdup ("x509");
  self->cert_data = g_ptr_array_new_with_free_func((GDestroyNotify) g_array_unref);
  self->rejections = g_ptr_array_new ();
}

static void
mock_tls_certificate_get_property (GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
  MockTLSCertificate *self = MOCK_TLS_CERTIFICATE (object);

  switch (property_id)
    {
    case PROP_STATE:
      g_value_set_uint (value, self->state);
      break;
    case PROP_REJECTIONS:
      g_value_set_boxed (value, self->rejections);
      break;
    case PROP_CERTIFICATE_TYPE:
      g_value_set_string (value, self->cert_type);
      break;
    case PROP_CERTIFICATE_CHAIN_DATA:
      g_value_set_boxed (value, self->cert_data);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
mock_tls_certificate_finalize (GObject *object)
{
  MockTLSCertificate *self = MOCK_TLS_CERTIFICATE (object);

  tp_clear_boxed (TP_ARRAY_TYPE_TLS_CERTIFICATE_REJECTION_LIST,
      &self->rejections);
  g_free (self->cert_type);
  self->cert_type = NULL;
  g_ptr_array_free (self->cert_data, TRUE);
  self->cert_data = NULL;

  G_OBJECT_CLASS (mock_tls_certificate_parent_class)->finalize (object);
}

static void
mock_tls_certificate_class_init (MockTLSCertificateClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  static TpDBusPropertiesMixinPropImpl object_props[] = {
          { "State", "state", NULL },
          { "Rejections", "rejections", NULL },
          { "CertificateType", "certificate-type", NULL },
          { "CertificateChainData", "certificate-chain-data", NULL },
          { NULL }
  };

  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
    { TP_IFACE_AUTHENTICATION_TLS_CERTIFICATE,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      object_props,
    },
    { NULL }
  };

  oclass->get_property = mock_tls_certificate_get_property;
  oclass->finalize = mock_tls_certificate_finalize;

  pspec = g_param_spec_uint ("state",
      "State of this certificate",
      "The state of this TLS certificate.",
      0, NUM_TP_TLS_CERTIFICATE_STATES - 1,
      TP_TLS_CERTIFICATE_STATE_PENDING,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_STATE, pspec);

  pspec = g_param_spec_boxed ("rejections",
      "The reject reasons",
      "The reasons why this TLS certificate has been rejected",
      TP_ARRAY_TYPE_TLS_CERTIFICATE_REJECTION_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_REJECTIONS, pspec);

  pspec = g_param_spec_string ("certificate-type",
      "The certificate type",
      "The type of this certificate.",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_CERTIFICATE_TYPE, pspec);

  pspec = g_param_spec_boxed ("certificate-chain-data",
      "The certificate chain data",
      "The raw PEM-encoded trust chain of this certificate.",
      TP_ARRAY_TYPE_UCHAR_ARRAY_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_CERTIFICATE_CHAIN_DATA, pspec);

  klass->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (oclass,
      G_STRUCT_OFFSET (MockTLSCertificateClass, dbus_props_class));
}

static void
mock_tls_certificate_accept (TpSvcAuthenticationTLSCertificate *base,
        DBusGMethodInvocation *context)
{
  MockTLSCertificate *self = MOCK_TLS_CERTIFICATE (base);
  self->state = TP_TLS_CERTIFICATE_STATE_ACCEPTED;
  tp_svc_authentication_tls_certificate_emit_accepted (self);
  tp_svc_authentication_tls_certificate_return_from_accept (context);
}

static void
mock_tls_certificate_reject (TpSvcAuthenticationTLSCertificate *base,
        const GPtrArray *in_Rejections, DBusGMethodInvocation *context)
{
  MockTLSCertificate *self = MOCK_TLS_CERTIFICATE (base);
  self->state = TP_TLS_CERTIFICATE_STATE_REJECTED;
  tp_svc_authentication_tls_certificate_emit_rejected (self, in_Rejections);
  tp_svc_authentication_tls_certificate_return_from_reject (context);
}

static void
mock_tls_certificate_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcAuthenticationTLSCertificateClass *klass =
    (TpSvcAuthenticationTLSCertificateClass*)g_iface;

  tp_svc_authentication_tls_certificate_implement_accept (klass,
      mock_tls_certificate_accept);
  tp_svc_authentication_tls_certificate_implement_reject (klass,
      mock_tls_certificate_reject);
}

static MockTLSCertificate*
mock_tls_certificate_new_and_register (TpDBusDaemon *dbus, const gchar *path, ...)
{
  MockTLSCertificate *cert;
  GError *error = NULL;
  gchar *filename, *contents;
  GArray *der;
  gsize length;
  va_list va;

  cert = g_object_new (mock_tls_certificate_get_type (), NULL);

  va_start (va, path);
  while (path != NULL) {
      filename = g_build_filename (g_getenv ("EMPATHY_SRCDIR"),
              "tests", "certificates", path, NULL);
      g_file_get_contents (filename, &contents, &length, &error);
      g_assert_no_error (error);

      der = g_array_sized_new (TRUE, TRUE, sizeof (guchar), length);
      g_array_append_vals (der, contents, length);
      g_ptr_array_add (cert->cert_data, der);

      g_free (contents);
      g_free (filename);

      path = va_arg (va, gchar*);
  }
  va_end (va);

  tp_dbus_daemon_register_object (dbus, MOCK_TLS_CERTIFICATE_PATH, cert);
  return cert;
}

/* ----------------------------------------------------------------------------
 * TESTS
 */

typedef struct {
  GMainLoop *loop;
  TpDBusDaemon *dbus;
  const gchar *dbus_name;
  MockTLSCertificate *mock;
} Test;

static void
setup (Test *test, gconstpointer data)
{
  GError *error = NULL;
  test->loop = g_main_loop_new (NULL, FALSE);

  test->dbus = tp_dbus_daemon_dup (&error);
  g_assert_no_error (error);

  test->dbus_name = tp_dbus_daemon_get_unique_name (test->dbus);
}

static void
teardown (Test *test, gconstpointer data)
{
  test->dbus_name = NULL;

  if (test->mock)
    {
      tp_dbus_daemon_unregister_object (test->dbus, test->mock);
      g_object_unref (test->mock);
      test->mock = NULL;
    }

  g_main_loop_unref (test->loop);
  test->loop = NULL;
}

static void
accepted_callback (GObject *object, GAsyncResult *res, gpointer user_data)
{
  GError *error = NULL;
  Test *test = user_data;

  g_assert (EMPATHY_IS_TLS_CERTIFICATE (object));
  empathy_tls_certificate_accept_finish (EMPATHY_TLS_CERTIFICATE (object),
          res, &error);
  g_assert_no_error (error);

  g_main_loop_quit (test->loop);
}

static void
prepared_callback (GObject *object, GAsyncResult *res, gpointer user_data)
{
  GError *error = NULL;
  Test *test = user_data;

  g_assert (EMPATHY_IS_TLS_CERTIFICATE (object));
  empathy_tls_certificate_prepare_finish (EMPATHY_TLS_CERTIFICATE (object),
          res, &error);
  g_assert_no_error (error);

  g_main_loop_quit (test->loop);
}

/* A simple test to make sure the test infrastructure is working */
static void
test_certificate_mock_basics (Test *test, gconstpointer data G_GNUC_UNUSED)
{
  GError *error = NULL;
  EmpathyTLSCertificate *cert;

  test->mock = mock_tls_certificate_new_and_register (test->dbus,
          "dhansak-collabora.cer");

  cert = empathy_tls_certificate_new (test->dbus, test->dbus_name,
          MOCK_TLS_CERTIFICATE_PATH, &error);
  g_assert_no_error (error);

  empathy_tls_certificate_prepare_async (cert, prepared_callback, test);
  g_main_loop_run (test->loop);

  empathy_tls_certificate_accept_async (cert, accepted_callback, test);
  g_main_loop_run (test->loop);

  g_object_unref (cert);
  g_assert (test->mock->state == TP_TLS_CERTIFICATE_STATE_ACCEPTED);
}

static void
verifier_callback (GObject *object, GAsyncResult *res, gpointer user_data)
{
  EmpTLSCertificateRejectReason reason = 0;
  GHashTable *details = NULL;
  GError *error = NULL;
  Test *test = user_data;

  g_assert (EMPATHY_IS_TLS_VERIFIER (object));
  empathy_tls_verifier_verify_finish (EMPATHY_TLS_VERIFIER (object),
          res, &reason, &details, &error);
  g_assert_no_error (error);

  if (details)
    g_hash_table_destroy (details);
  g_main_loop_quit (test->loop);
}

static void
test_certificate_verify (Test *test, gconstpointer data G_GNUC_UNUSED)
{
  GError *error = NULL;
  EmpathyTLSCertificate *cert;
  EmpathyTLSVerifier *verifier;

  test->mock = mock_tls_certificate_new_and_register (test->dbus,
          "dhansak-collabora.cer");

  cert = empathy_tls_certificate_new (test->dbus, test->dbus_name,
          MOCK_TLS_CERTIFICATE_PATH, &error);
  g_assert_no_error (error);

  empathy_tls_certificate_prepare_async (cert, prepared_callback, test);
  g_main_loop_run (test->loop);

  verifier = empathy_tls_verifier_new (cert, "another-host");

  empathy_tls_verifier_verify_async (verifier, verifier_callback, test);
  g_main_loop_run (test->loop);

#if 0
  empathy_tls_certificate_accept_async (cert, accepted_callback, test);
  g_main_loop_run (test->loop);
#endif

  g_object_unref (verifier);
  g_object_unref (cert);
}

int
main (int argc,
    char **argv)
{
  int result;

  test_init (argc, argv);
  gnutls_global_init ();

  g_test_add ("/tls/certificate_basics", Test, NULL,
          setup, test_certificate_mock_basics, teardown);
  g_test_add ("/tls/certificate_verify", Test, NULL,
          setup, test_certificate_verify, teardown);

  result = g_test_run ();
  test_deinit ();
  return result;
}
