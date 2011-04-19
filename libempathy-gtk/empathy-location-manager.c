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

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyLocationManager)
typedef struct {
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
} EmpathyLocationManagerPriv;

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
  EmpathyLocationManagerPriv *priv = GET_PRIV (object);
  void (*dispose) (GObject *) =
    G_OBJECT_CLASS (empathy_location_manager_parent_class)->dispose;

  if (priv->account_manager != NULL)
  {
    g_object_unref (priv->account_manager);
    priv->account_manager = NULL;
  }

  if (priv->gsettings_loc != NULL)
  {
    g_object_unref (priv->gsettings_loc);
    priv->gsettings_loc = NULL;
  }

  if (priv->gc_client != NULL)
  {
    g_object_unref (priv->gc_client);
    priv->gc_client = NULL;
  }

  if (priv->gc_position != NULL)
  {
    g_object_unref (priv->gc_position);
    priv->gc_position = NULL;
  }

  if (priv->gc_address != NULL)
  {
    g_object_unref (priv->gc_address);
    priv->gc_address = NULL;
  }

  if (priv->location != NULL)
  {
    g_hash_table_unref (priv->location);
    priv->location = NULL;
  }

  if (dispose != NULL)
    dispose (object);
}

static void
location_manager_get_property (GObject *object,
                      guint param_id,
                      GValue *value,
                      GParamSpec *pspec)
{
  /*EmpathyLocationManagerPriv *priv = GET_PRIV (object); */

  switch (param_id)
    {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    };
}

static void
location_manager_set_property (GObject *object,
                      guint param_id,
                      const GValue *value,
                      GParamSpec *pspec)
{
  /* EmpathyLocationManagerPriv *priv = GET_PRIV (object); */

  switch (param_id)
    {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    };
}

static void
empathy_location_manager_class_init (EmpathyLocationManagerClass *class)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (class);

  object_class->constructor = location_manager_constructor;
  object_class->dispose = location_manager_dispose;
  object_class->get_property = location_manager_get_property;
  object_class->set_property = location_manager_set_property;

  g_type_class_add_private (object_class, sizeof (EmpathyLocationManagerPriv));
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
  EmpathyLocationManagerPriv *priv = GET_PRIV (self);
  guint connection_status = -1;

  if (!conn)
    return;

  if (!force_publication)
    {
      if (!g_settings_get_boolean (priv->gsettings_loc,
            EMPATHY_PREFS_LOCATION_PUBLISH))
        return;
    }

  connection_status = tp_connection_get_status (conn, NULL);

  if (connection_status != TP_CONNECTION_STATUS_CONNECTED)
    return;

  DEBUG ("Publishing %s location to connection %p",
      (g_hash_table_size (priv->location) == 0 ? "empty" : ""),
      conn);

  tp_cli_connection_interface_location_call_set_location (conn, -1,
      priv->location, publish_location_cb, NULL, NULL, G_OBJECT (self));
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
  EmpathyLocationManagerPriv *priv = GET_PRIV (self);
  PublishToAllData *data;

  data = g_slice_new0 (PublishToAllData);
  data->self = g_object_ref (self);
  data->force_publication = force_publication;

  tp_account_manager_prepare_async (priv->account_manager, NULL,
      publish_to_all_am_prepared_cb, data);
}

static gboolean
publish_on_idle (gpointer user_data)
{
  EmpathyLocationManager *manager = EMPATHY_LOCATION_MANAGER (user_data);
  EmpathyLocationManagerPriv *priv = GET_PRIV (manager);

  priv->timeout_id = 0;
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
    gpointer *self)
{
  EmpathyLocationManagerPriv *priv = GET_PRIV (self);
  TpConnection *conn;

  conn = tp_account_get_connection (account);

  DEBUG ("New connection %p", conn);

  /* Don't publish if it is already planned (ie startup) */
  if (priv->timeout_id == 0)
    {
      publish_location (EMPATHY_LOCATION_MANAGER (self), conn,
          FALSE);
    }
}

static void
update_timestamp (EmpathyLocationManager *self)
{
  EmpathyLocationManagerPriv *priv= GET_PRIV (self);
  gint64 timestamp;

  timestamp = empathy_time_get_current ();
  tp_asv_set_int64 (priv->location, EMPATHY_LOCATION_TIMESTAMP, timestamp);

  DEBUG ("\t - Timestamp: %" G_GINT64_FORMAT, timestamp);
}

static void
address_changed_cb (GeoclueAddress *address,
                    int timestamp,
                    GHashTable *details,
                    GeoclueAccuracy *accuracy,
                    gpointer self)
{
  GeoclueAccuracyLevel level;
  EmpathyLocationManagerPriv *priv = GET_PRIV (self);
  GHashTableIter iter;
  gpointer key, value;

  geoclue_accuracy_get_details (accuracy, &level, NULL, NULL);
  DEBUG ("New address (accuracy level %d):", level);
  /* FIXME: Publish accuracy level also considering the position's */

  g_hash_table_remove (priv->location, EMPATHY_LOCATION_STREET);
  g_hash_table_remove (priv->location, EMPATHY_LOCATION_AREA);
  g_hash_table_remove (priv->location, EMPATHY_LOCATION_REGION);
  g_hash_table_remove (priv->location, EMPATHY_LOCATION_COUNTRY);
  g_hash_table_remove (priv->location, EMPATHY_LOCATION_COUNTRY_CODE);
  g_hash_table_remove (priv->location, EMPATHY_LOCATION_POSTAL_CODE);

  if (g_hash_table_size (details) == 0)
    {
      DEBUG ("\t - (Empty)");
      return;
    }

  g_hash_table_iter_init (&iter, details);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      GValue *new_value;
      /* Discard street information if reduced accuracy is on */
      if (priv->reduce_accuracy &&
          !tp_strdiff (key, EMPATHY_LOCATION_STREET))
        continue;

      new_value = tp_g_value_slice_new_string (value);
      g_hash_table_insert (priv->location, g_strdup (key), new_value);

      DEBUG ("\t - %s: %s", (gchar *) key, (gchar *) value);
    }

  update_timestamp (self);
  if (priv->timeout_id == 0)
    priv->timeout_id = g_timeout_add_seconds (TIMEOUT, publish_on_idle, self);
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
                     gpointer self)
{
  EmpathyLocationManagerPriv *priv = GET_PRIV (self);
  GeoclueAccuracyLevel level;
  gdouble mean, horizontal, vertical;
  GValue *new_value;


  geoclue_accuracy_get_details (accuracy, &level, &horizontal, &vertical);
  DEBUG ("New position (accuracy level %d)", level);
  if (level == GEOCLUE_ACCURACY_LEVEL_NONE)
    return;

  if (fields & GEOCLUE_POSITION_FIELDS_LONGITUDE)
    {

      if (priv->reduce_accuracy)
        /* Truncate at 1 decimal place */
        longitude = ((int) (longitude * 10)) / 10.0;

      new_value = tp_g_value_slice_new_double (longitude);
      g_hash_table_insert (priv->location, g_strdup (EMPATHY_LOCATION_LON),
          new_value);
      DEBUG ("\t - Longitude: %f", longitude);
    }
  else
    {
      g_hash_table_remove (priv->location, EMPATHY_LOCATION_LON);
    }

  if (fields & GEOCLUE_POSITION_FIELDS_LATITUDE)
    {
      if (priv->reduce_accuracy)
        /* Truncate at 1 decimal place */
        latitude = ((int) (latitude * 10)) / 10.0;

      new_value = tp_g_value_slice_new_double (latitude);
      g_hash_table_replace (priv->location, g_strdup (EMPATHY_LOCATION_LAT),
          new_value);
      DEBUG ("\t - Latitude: %f", latitude);
    }
  else
    {
      g_hash_table_remove (priv->location, EMPATHY_LOCATION_LAT);
    }

  if (fields & GEOCLUE_POSITION_FIELDS_ALTITUDE)
    {
      new_value = tp_g_value_slice_new_double (altitude);
      g_hash_table_replace (priv->location, g_strdup (EMPATHY_LOCATION_ALT),
          new_value);
      DEBUG ("\t - Altitude: %f", altitude);
    }
  else
    {
      g_hash_table_remove (priv->location, EMPATHY_LOCATION_ALT);
    }

  if (level == GEOCLUE_ACCURACY_LEVEL_DETAILED)
    {
      mean = (horizontal + vertical) / 2.0;
      new_value = tp_g_value_slice_new_double (mean);
      g_hash_table_replace (priv->location,
          g_strdup (EMPATHY_LOCATION_ACCURACY), new_value);
      DEBUG ("\t - Accuracy: %f", mean);
    }
  else
    {
      g_hash_table_remove (priv->location, EMPATHY_LOCATION_ACCURACY);
    }

  update_timestamp (self);
  if (priv->timeout_id == 0)
    priv->timeout_id = g_timeout_add_seconds (TIMEOUT, publish_on_idle, self);
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
  EmpathyLocationManagerPriv *priv = GET_PRIV (self);
  GError *error = NULL;

  if (!geoclue_master_client_set_requirements (priv->gc_client,
          GEOCLUE_ACCURACY_LEVEL_COUNTRY, 0, FALSE, priv->resources,
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
  EmpathyLocationManagerPriv *priv = GET_PRIV (self);

  DEBUG ("Updating resources %d", priv->resources);

  if (!priv->geoclue_is_setup)
    return;

  /* As per Geoclue bug #15126, using NONE results in no address
   * being found as geoclue-manual report an empty address with
   * accuracy = NONE */
  if (!set_requirements (self))
    return;

  geoclue_address_get_address_async (priv->gc_address,
      initial_address_cb, self);
  geoclue_position_get_position_async (priv->gc_position,
      initial_position_cb, self);
}

static void
setup_geoclue (EmpathyLocationManager *self)
{
  EmpathyLocationManagerPriv *priv = GET_PRIV (self);

  GeoclueMaster *master;
  GError *error = NULL;

  DEBUG ("Setting up Geoclue");
  master = geoclue_master_get_default ();
  priv->gc_client = geoclue_master_create_client (master, NULL, &error);
  g_object_unref (master);

  if (priv->gc_client == NULL)
    {
      DEBUG ("Failed to GeoclueMasterClient: %s", error->message);
      g_error_free (error);
      return;
    }

  if (!set_requirements (self))
    return;

  /* Get updated when the position is changes */
  priv->gc_position = geoclue_master_client_create_position (
      priv->gc_client, &error);
  if (priv->gc_position == NULL)
    {
      DEBUG ("Failed to create GeocluePosition: %s", error->message);
      g_error_free (error);
      return;
    }

  g_signal_connect (G_OBJECT (priv->gc_position), "position-changed",
      G_CALLBACK (position_changed_cb), self);

  /* Get updated when the address changes */
  priv->gc_address = geoclue_master_client_create_address (
      priv->gc_client, &error);
  if (priv->gc_address == NULL)
    {
      DEBUG ("Failed to create GeoclueAddress: %s", error->message);
      g_error_free (error);
      return;
    }

  g_signal_connect (G_OBJECT (priv->gc_address), "address-changed",
      G_CALLBACK (address_changed_cb), self);

  priv->geoclue_is_setup = TRUE;
}

static void
publish_cb (GSettings *gsettings_loc,
            const gchar *key,
            gpointer user_data)
{
  EmpathyLocationManager *manager = EMPATHY_LOCATION_MANAGER (user_data);
  EmpathyLocationManagerPriv *priv = GET_PRIV (manager);

  DEBUG ("Publish Conf changed");

  if (g_settings_get_boolean (gsettings_loc, key))
    {
      if (!priv->geoclue_is_setup)
        setup_geoclue (manager);
      /* if still not setup than the init failed */
      if (!priv->geoclue_is_setup)
        return;

      geoclue_address_get_address_async (priv->gc_address,
          initial_address_cb, manager);
      geoclue_position_get_position_async (priv->gc_position,
          initial_position_cb, manager);
    }
  else
    {
      /* As per XEP-0080: send an empty location to have remove current
       * location from the servers
       */
      g_hash_table_remove_all (priv->location);
      publish_to_all_connections (manager, TRUE);
    }

}

static void
resource_cb (GSettings *gsettings_loc,
             const gchar *key,
             gpointer user_data)
{
  EmpathyLocationManager *manager = EMPATHY_LOCATION_MANAGER (user_data);
  EmpathyLocationManagerPriv *priv = GET_PRIV (manager);
  GeoclueResourceFlags resource = 0;

  DEBUG ("%s changed", key);

  if (!tp_strdiff (key, EMPATHY_PREFS_LOCATION_RESOURCE_NETWORK))
    resource = GEOCLUE_RESOURCE_NETWORK;
  if (!tp_strdiff (key, EMPATHY_PREFS_LOCATION_RESOURCE_CELL))
    resource = GEOCLUE_RESOURCE_CELL;
  if (!tp_strdiff (key, EMPATHY_PREFS_LOCATION_RESOURCE_GPS))
    resource = GEOCLUE_RESOURCE_GPS;

  if (g_settings_get_boolean (gsettings_loc, key))
    priv->resources |= resource;
  else
    priv->resources &= ~resource;

  if (priv->geoclue_is_setup)
    update_resources (manager);
}

static void
accuracy_cb (GSettings *gsettings_loc,
             const gchar *key,
             gpointer user_data)
{
  EmpathyLocationManager *manager = EMPATHY_LOCATION_MANAGER (user_data);
  EmpathyLocationManagerPriv *priv = GET_PRIV (manager);

  DEBUG ("%s changed", key);

  priv->reduce_accuracy = g_settings_get_boolean (gsettings_loc, key);

  if (!priv->geoclue_is_setup)
    return;

  geoclue_address_get_address_async (priv->gc_address,
      initial_address_cb, manager);
  geoclue_position_get_position_async (priv->gc_position,
      initial_position_cb, manager);
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
  EmpathyLocationManagerPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_LOCATION_MANAGER, EmpathyLocationManagerPriv);

  self->priv = priv;
  priv->geoclue_is_setup = FALSE;
  priv->location = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      g_free, (GDestroyNotify) tp_g_value_slice_free);
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
