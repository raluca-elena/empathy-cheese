/*
 * Copyright (C) 2009 Collabora Ltd.
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
 * Authors: Pierre-Luc Beaudoin <pierre-luc.beaudoin@collabora.co.uk>
 */

#include "config.h"

#include <string.h>
#include <time.h>

#include <glib/gi18n.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/util.h>

#include <geoclue/geoclue-master.h>

#include <extensions/extensions.h>

#include "empathy-location-manager.h"

#include "libempathy/empathy-enum-types.h"
#include "libempathy/empathy-gsettings.h"
#include "libempathy/empathy-location.h"
#include "libempathy/empathy-utils.h"
#include "libempathy/empathy-time.h"

#define DEBUG_FLAG EMPATHY_DEBUG_LOCATION
#include "libempathy/empathy-debug.h"

/* Seconds before updating the location */
#define TIMEOUT 10
static EmpathyLocationManager *location_manager = NULL;

struct _EmpathyLocationManagerPrivate {
    gboolean geoclue_is_setup;
    /* Contains the location to be sent to accounts.  Geoclue is used
     * to populate it.  This HashTable uses Telepathy's style (string,
     * GValue). Keys are defined in empathy-location.h
     */
    GHashTable *location;

    GSettings *gsettings_loc;

    GeoclueResourceFlags resources;
    GeoclueMasterClient *gc_client;
    GeocluePosition *gc_position;
    GeoclueAddress *gc_address;

    gboolean reduce_accuracy;
    TpAccountManager *account_manager;

    /* The idle id for publish_on_idle func */
    guint timeout_id;
};

G_DEFINE_TYPE (EmpathyLocationManager, empathy_location_manager, G_TYPE_OBJECT);

static GObject *
location_manager_constructor (GType type,
    guint n_construct_params,
    GObjectConstructParam *construct_params)
{
  GObject *retval;

  if (location_manager == NULL)
    {
      retval = G_OBJECT_CLASS (empathy_location_manager_parent_class)->constructor
          (type, n_construct_params, construct_params);

      location_manager = EMPATHY_LOCATION_MANAGER (retval);
      g_object_add_weak_pointer (retval, (gpointer) &location_manager);
    }
  else
    {
      retval = g_object_ref (location_manager);
    }

  return retval;
}

static void
location_manager_dispose (GObject *object)
{
  EmpathyLocationManager *self = (EmpathyLocationManager *) object;
  void (*dispose) (GObject *) =
    G_OBJECT_CLASS (empathy_location_manager_parent_class)->dispose;

  tp_clear_object (&self->priv->account_manager);
  tp_clear_object (&self->priv->gsettings_loc);
  tp_clear_object (&self->priv->gc_client);
  tp_clear_object (&self->priv->gc_position);
  tp_clear_object (&self->priv->gc_address);
  tp_clear_pointer (&self->priv->location, g_hash_table_unref);

  if (dispose != NULL)
    dispose (object);
}

static void
empathy_location_manager_class_init (EmpathyLocationManagerClass *class)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (class);

  object_class->constructor = location_manager_constructor;
  object_class->dispose = location_manager_dispose;

  g_type_class_add_private (object_class, sizeof (EmpathyLocationManagerPrivate));
}

static void
publish_location_cb (TpConnection *connection,
                     const GError *error,
                     gpointer user_data,
                     GObject *weak_object)
{
  if (error != NULL)
      DEBUG ("Error setting location: %s", error->message);
}

static void
publish_location (EmpathyLocationManager *self,
    TpConnection *conn,
    gboolean force_publication)
{
  guint connection_status = -1;

  if (!conn)
    return;

  if (!force_publication)
    {
      if (!g_settings_get_boolean (self->priv->gsettings_loc,
            EMPATHY_PREFS_LOCATION_PUBLISH))
        return;
    }

  connection_status = tp_connection_get_status (conn, NULL);

  if (connection_status != TP_CONNECTION_STATUS_CONNECTED)
    return;

  DEBUG ("Publishing %s location to connection %p",
      (g_hash_table_size (self->priv->location) == 0 ? "empty" : ""),
      conn);

  tp_cli_connection_interface_location_call_set_location (conn, -1,
      self->priv->location, publish_location_cb, NULL, NULL, G_OBJECT (self));
}

typedef struct
{
  EmpathyLocationManager *self;
  gboolean force_publication;
} PublishToAllData;

static void
publish_to_all_am_prepared_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (source_object);
  PublishToAllData *data = user_data;
  GList *accounts, *l;
  GError *error = NULL;

  if (!tp_account_manager_prepare_finish (manager, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      goto out;
    }

  accounts = tp_account_manager_get_valid_accounts (manager);
  for (l = accounts; l; l = l->next)
    {
      TpConnection *conn = tp_account_get_connection (TP_ACCOUNT (l->data));

      if (conn != NULL)
        publish_location (data->self, conn, data->force_publication);
    }
  g_list_free (accounts);

out:
  g_object_unref (data->self);
  g_slice_free (PublishToAllData, data);
}

static void
publish_to_all_connections (EmpathyLocationManager *self,
    gboolean force_publication)
{
  PublishToAllData *data;

  data = g_slice_new0 (PublishToAllData);
  data->self = g_object_ref (self);
  data->force_publication = force_publication;

  tp_account_manager_prepare_async (self->priv->account_manager, NULL,
      publish_to_all_am_prepared_cb, data);
}

static gboolean
publish_on_idle (gpointer user_data)
{
  EmpathyLocationManager *manager = EMPATHY_LOCATION_MANAGER (user_data);

  manager->priv->timeout_id = 0;
  publish_to_all_connections (manager, TRUE);
  return FALSE;
}

static void
new_connection_cb (TpAccount *account,
    guint old_status,
    guint new_status,
    guint reason,
    gchar *dbus_error_name,
    GHashTable *details,
    gpointer user_data)
{
  EmpathyLocationManager *self = user_data;
  TpConnection *conn;

  conn = tp_account_get_connection (account);

  DEBUG ("New connection %p", conn);

  /* Don't publish if it is already planned (ie startup) */
  if (self->priv->timeout_id == 0)
    {
      publish_location (EMPATHY_LOCATION_MANAGER (self), conn,
          FALSE);
    }
}

static void
update_timestamp (EmpathyLocationManager *self)
{
  gint64 timestamp;

  timestamp = empathy_time_get_current ();
  tp_asv_set_int64 (self->priv->location, EMPATHY_LOCATION_TIMESTAMP,
      timestamp);

  DEBUG ("\t - Timestamp: %" G_GINT64_FORMAT, timestamp);
}

static void
address_changed_cb (GeoclueAddress *address,
                    int timestamp,
                    GHashTable *details,
                    GeoclueAccuracy *accuracy,
                    gpointer user_data)
{
  EmpathyLocationManager *self = user_data;
  GeoclueAccuracyLevel level;
  GHashTableIter iter;
  gpointer key, value;

  geoclue_accuracy_get_details (accuracy, &level, NULL, NULL);
  DEBUG ("New address (accuracy level %d):", level);
  /* FIXME: Publish accuracy level also considering the position's */

  g_hash_table_remove (self->priv->location, EMPATHY_LOCATION_STREET);
  g_hash_table_remove (self->priv->location, EMPATHY_LOCATION_AREA);
  g_hash_table_remove (self->priv->location, EMPATHY_LOCATION_REGION);
  g_hash_table_remove (self->priv->location, EMPATHY_LOCATION_COUNTRY);
  g_hash_table_remove (self->priv->location, EMPATHY_LOCATION_COUNTRY_CODE);
  g_hash_table_remove (self->priv->location, EMPATHY_LOCATION_POSTAL_CODE);

  if (g_hash_table_size (details) == 0)
    {
      DEBUG ("\t - (Empty)");
      return;
    }

  g_hash_table_iter_init (&iter, details);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      /* Discard street information if reduced accuracy is on */
      if (self->priv->reduce_accuracy &&
          !tp_strdiff (key, EMPATHY_LOCATION_STREET))
        continue;

      tp_asv_set_string (self->priv->location, key, value);

      DEBUG ("\t - %s: %s", (gchar *) key, (gchar *) value);
    }

  update_timestamp (self);
  if (self->priv->timeout_id == 0)
    self->priv->timeout_id = g_timeout_add_seconds (TIMEOUT, publish_on_idle,
        self);
}

static void
initial_address_cb (GeoclueAddress *address,
                    int timestamp,
                    GHashTable *details,
                    GeoclueAccuracy *accuracy,
                    GError *error,
                    gpointer self)
{
  if (error)
    {
      DEBUG ("Error: %s", error->message);
      g_error_free (error);
    }
  else
    {
      address_changed_cb (address, timestamp, details, accuracy, self);
    }
}

static void
position_changed_cb (GeocluePosition *position,
                     GeocluePositionFields fields,
                     int timestamp,
                     double latitude,
                     double longitude,
                     double altitude,
                     GeoclueAccuracy *accuracy,
                     gpointer user_data)
{
  EmpathyLocationManager *self = user_data;
  GeoclueAccuracyLevel level;
  gdouble mean, horizontal, vertical;

  geoclue_accuracy_get_details (accuracy, &level, &horizontal, &vertical);
  DEBUG ("New position (accuracy level %d)", level);
  if (level == GEOCLUE_ACCURACY_LEVEL_NONE)
    return;

  if (fields & GEOCLUE_POSITION_FIELDS_LONGITUDE)
    {

      if (self->priv->reduce_accuracy)
        /* Truncate at 1 decimal place */
        longitude = ((int) (longitude * 10)) / 10.0;

      tp_asv_set_double (self->priv->location, EMPATHY_LOCATION_LON, longitude);

      DEBUG ("\t - Longitude: %f", longitude);
    }
  else
    {
      g_hash_table_remove (self->priv->location, EMPATHY_LOCATION_LON);
    }

  if (fields & GEOCLUE_POSITION_FIELDS_LATITUDE)
    {
      if (self->priv->reduce_accuracy)
        /* Truncate at 1 decimal place */
        latitude = ((int) (latitude * 10)) / 10.0;

      tp_asv_set_double (self->priv->location, EMPATHY_LOCATION_LAT, latitude);

      DEBUG ("\t - Latitude: %f", latitude);
    }
  else
    {
      g_hash_table_remove (self->priv->location, EMPATHY_LOCATION_LAT);
    }

  if (fields & GEOCLUE_POSITION_FIELDS_ALTITUDE)
    {
      tp_asv_set_double (self->priv->location, EMPATHY_LOCATION_ALT, altitude);

      DEBUG ("\t - Altitude: %f", altitude);
    }
  else
    {
      g_hash_table_remove (self->priv->location, EMPATHY_LOCATION_ALT);
    }

  if (level == GEOCLUE_ACCURACY_LEVEL_DETAILED)
    {
      mean = (horizontal + vertical) / 2.0;
      tp_asv_set_double (self->priv->location, EMPATHY_LOCATION_ACCURACY, mean);

      DEBUG ("\t - Accuracy: %f", mean);
    }
  else
    {
      g_hash_table_remove (self->priv->location, EMPATHY_LOCATION_ACCURACY);
    }

  update_timestamp (self);
  if (self->priv->timeout_id == 0)
    self->priv->timeout_id = g_timeout_add_seconds (TIMEOUT, publish_on_idle,
        self);
}

static void
initial_position_cb (GeocluePosition *position,
                     GeocluePositionFields fields,
                     int timestamp,
                     double latitude,
                     double longitude,
                     double altitude,
                     GeoclueAccuracy *accuracy,
                     GError *error,
                     gpointer self)
{
  if (error)
    {
      DEBUG ("Error: %s", error->message);
      g_error_free (error);
    }
  else
    {
      position_changed_cb (position, fields, timestamp, latitude, longitude,
          altitude, accuracy, self);
    }
}

static gboolean
set_requirements (EmpathyLocationManager *self)
{
  GError *error = NULL;

  if (!geoclue_master_client_set_requirements (self->priv->gc_client,
          GEOCLUE_ACCURACY_LEVEL_COUNTRY, 0, FALSE, self->priv->resources,
          &error))
    {
      DEBUG ("set_requirements failed: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  return TRUE;
}

static void
update_resources (EmpathyLocationManager *self)
{
  DEBUG ("Updating resources %d", self->priv->resources);

  if (!self->priv->geoclue_is_setup)
    return;

  /* As per Geoclue bug #15126, using NONE results in no address
   * being found as geoclue-manual report an empty address with
   * accuracy = NONE */
  if (!set_requirements (self))
    return;

  geoclue_address_get_address_async (self->priv->gc_address,
      initial_address_cb, self);
  geoclue_position_get_position_async (self->priv->gc_position,
      initial_position_cb, self);
}

static void
setup_geoclue (EmpathyLocationManager *self)
{
  GeoclueMaster *master;
  GError *error = NULL;

  DEBUG ("Setting up Geoclue");
  master = geoclue_master_get_default ();
  self->priv->gc_client = geoclue_master_create_client (master, NULL, &error);
  g_object_unref (master);

  if (self->priv->gc_client == NULL)
    {
      DEBUG ("Failed to GeoclueMasterClient: %s", error->message);
      g_error_free (error);
      return;
    }

  if (!set_requirements (self))
    return;

  /* Get updated when the position is changes */
  self->priv->gc_position = geoclue_master_client_create_position (
      self->priv->gc_client, &error);
  if (self->priv->gc_position == NULL)
    {
      DEBUG ("Failed to create GeocluePosition: %s", error->message);
      g_error_free (error);
      return;
    }

  g_signal_connect (G_OBJECT (self->priv->gc_position), "position-changed",
      G_CALLBACK (position_changed_cb), self);

  /* Get updated when the address changes */
  self->priv->gc_address = geoclue_master_client_create_address (
      self->priv->gc_client, &error);
  if (self->priv->gc_address == NULL)
    {
      DEBUG ("Failed to create GeoclueAddress: %s", error->message);
      g_error_free (error);
      return;
    }

  g_signal_connect (G_OBJECT (self->priv->gc_address), "address-changed",
      G_CALLBACK (address_changed_cb), self);

  self->priv->geoclue_is_setup = TRUE;
}

static void
publish_cb (GSettings *gsettings_loc,
            const gchar *key,
            gpointer user_data)
{
  EmpathyLocationManager *self = EMPATHY_LOCATION_MANAGER (user_data);

  DEBUG ("Publish Conf changed");

  if (g_settings_get_boolean (gsettings_loc, key))
    {
      if (!self->priv->geoclue_is_setup)
        setup_geoclue (self);
      /* if still not setup than the init failed */
      if (!self->priv->geoclue_is_setup)
        return;

      geoclue_address_get_address_async (self->priv->gc_address,
          initial_address_cb, self);
      geoclue_position_get_position_async (self->priv->gc_position,
          initial_position_cb, self);
    }
  else
    {
      /* As per XEP-0080: send an empty location to have remove current
       * location from the servers
       */
      g_hash_table_remove_all (self->priv->location);
      publish_to_all_connections (self, TRUE);
    }

}

static void
resource_cb (GSettings *gsettings_loc,
             const gchar *key,
             gpointer user_data)
{
  EmpathyLocationManager *self = EMPATHY_LOCATION_MANAGER (user_data);
  GeoclueResourceFlags resource = 0;

  DEBUG ("%s changed", key);

  if (!tp_strdiff (key, EMPATHY_PREFS_LOCATION_RESOURCE_NETWORK))
    resource = GEOCLUE_RESOURCE_NETWORK;
  if (!tp_strdiff (key, EMPATHY_PREFS_LOCATION_RESOURCE_CELL))
    resource = GEOCLUE_RESOURCE_CELL;
  if (!tp_strdiff (key, EMPATHY_PREFS_LOCATION_RESOURCE_GPS))
    resource = GEOCLUE_RESOURCE_GPS;

  if (g_settings_get_boolean (gsettings_loc, key))
    self->priv->resources |= resource;
  else
    self->priv->resources &= ~resource;

  if (self->priv->geoclue_is_setup)
    update_resources (self);
}

static void
accuracy_cb (GSettings *gsettings_loc,
             const gchar *key,
             gpointer user_data)
{
  EmpathyLocationManager *self = EMPATHY_LOCATION_MANAGER (user_data);

  DEBUG ("%s changed", key);

  self->priv->reduce_accuracy = g_settings_get_boolean (gsettings_loc, key);

  if (!self->priv->geoclue_is_setup)
    return;

  geoclue_address_get_address_async (self->priv->gc_address,
      initial_address_cb, self);
  geoclue_position_get_position_async (self->priv->gc_position,
      initial_position_cb, self);
}

static void
account_manager_prepared_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  GList *accounts, *l;
  TpAccountManager *account_manager = TP_ACCOUNT_MANAGER (source_object);
  EmpathyLocationManager *self = user_data;
  GError *error = NULL;

  if (!tp_account_manager_prepare_finish (account_manager, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      return;
    }

  accounts = tp_account_manager_get_valid_accounts (account_manager);
  for (l = accounts; l != NULL; l = l->next)
    {
      TpAccount *account = TP_ACCOUNT (l->data);

      tp_g_signal_connect_object (account, "status-changed",
          G_CALLBACK (new_connection_cb), self, 0);
    }
  g_list_free (accounts);
}

static void
empathy_location_manager_init (EmpathyLocationManager *self)
{
  EmpathyLocationManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_LOCATION_MANAGER, EmpathyLocationManagerPrivate);

  self->priv = priv;
  priv->geoclue_is_setup = FALSE;
  priv->location = tp_asv_new (NULL, NULL);
  priv->gsettings_loc = g_settings_new (EMPATHY_PREFS_LOCATION_SCHEMA);

  /* Setup account status callbacks */
  priv->account_manager = tp_account_manager_dup ();

  tp_account_manager_prepare_async (priv->account_manager, NULL,
      account_manager_prepared_cb, self);

  /* Setup settings status callbacks */
  g_signal_connect (priv->gsettings_loc,
      "changed::" EMPATHY_PREFS_LOCATION_PUBLISH,
      G_CALLBACK (publish_cb), self);
  g_signal_connect (priv->gsettings_loc,
      "changed::" EMPATHY_PREFS_LOCATION_RESOURCE_NETWORK,
      G_CALLBACK (resource_cb), self);
  g_signal_connect (priv->gsettings_loc,
      "changed::" EMPATHY_PREFS_LOCATION_RESOURCE_CELL,
      G_CALLBACK (resource_cb), self);
  g_signal_connect (priv->gsettings_loc,
      "changed::" EMPATHY_PREFS_LOCATION_RESOURCE_GPS,
      G_CALLBACK (resource_cb), self);
  g_signal_connect (priv->gsettings_loc,
      "changed::" EMPATHY_PREFS_LOCATION_REDUCE_ACCURACY,
      G_CALLBACK (accuracy_cb), self);

  resource_cb (priv->gsettings_loc, EMPATHY_PREFS_LOCATION_RESOURCE_NETWORK,
      self);
  resource_cb (priv->gsettings_loc, EMPATHY_PREFS_LOCATION_RESOURCE_CELL, self);
  resource_cb (priv->gsettings_loc, EMPATHY_PREFS_LOCATION_RESOURCE_GPS, self);
  accuracy_cb (priv->gsettings_loc, EMPATHY_PREFS_LOCATION_REDUCE_ACCURACY,
      self);
  publish_cb (priv->gsettings_loc, EMPATHY_PREFS_LOCATION_PUBLISH, self);
}

EmpathyLocationManager *
empathy_location_manager_dup_singleton (void)
{
  return EMPATHY_LOCATION_MANAGER (g_object_new (EMPATHY_TYPE_LOCATION_MANAGER,
      NULL));
}
