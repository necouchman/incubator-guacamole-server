/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "config.h"

#include "auth.h"
#include "client.h"
#include "common/clipboard.h"
#include "common/cursor.h"
#include "common/display.h"
#include "common/recording.h"
#include "channels/audio.h"
#include "channels/clipboard.h"
#include "channels/cursor.h"
#include "channels/display.h"
#include "channels/file.h"
#include "log.h"
#include "settings.h"
#include "spice.h"
#include "spice-constants.h"

#ifdef ENABLE_COMMON_SSH
#include "common-ssh/sftp.h"
#include "common-ssh/ssh.h"
#include "sftp.h"
#endif

#include <guacamole/client.h>
#include <guacamole/protocol.h>
#include <guacamole/socket.h>
#include <guacamole/timestamp.h>
#include <spice-client-glib-2.0/spice-client.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

SpiceSession* guac_spice_get_session(guac_client* client) {

    guac_client_log(client, GUAC_LOG_DEBUG, "Initializing new SPICE session.");
    
    /* Set up the SPICE session and Guacamole client. */
    guac_spice_client* spice_client = (guac_spice_client*) client->data;
    guac_spice_settings* spice_settings = spice_client->settings;
    
    /* Create a new SPICE client. */
    SpiceSession* spice_session = spice_session_new();
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Registering new channel callback.");
    
    /* Register a callback for handling new channel events. */
    g_signal_connect(spice_session, SPICE_SIGNAL_CHANNEL_NEW,
            G_CALLBACK(guac_spice_client_channel_handler), client);
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Setting up connection properties.");

    g_object_set(spice_session, SPICE_PROPERTY_ENABLE_USBREDIR, FALSE, NULL);
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Setting up host/port.");
    
    /* Set hostname and port */
    g_object_set(spice_session, SPICE_PROPERTY_CLIENT_SOCKETS, FALSE, NULL);
    g_object_set(spice_session, SPICE_PROPERTY_HOST, spice_settings->hostname, NULL);
    g_object_set(spice_session, "uri", "spice://localhost?tls-port=55000", NULL);
    guac_client_log(client, GUAC_LOG_DEBUG, "Connecting to host %s", spice_settings->hostname);
    if (spice_settings->tls) {
        guac_client_log(client, GUAC_LOG_DEBUG, "Using TLS mode on port %s", spice_settings->port);
        g_object_set(spice_session,
                SPICE_PROPERTY_TLS_PORT, spice_settings->port,
                SPICE_PROPERTY_VERIFY, spice_settings->tls_verify,
                NULL);
        if (spice_settings->ca != NULL)
            g_object_set(spice_session, SPICE_PROPERTY_CA, spice_settings->ca, NULL);
        if (spice_settings->ca_file != NULL)
            g_object_set(spice_session, SPICE_PROPERTY_CA_FILE, spice_settings->ca_file, NULL);
    }
    else {
        guac_client_log(client, GUAC_LOG_DEBUG, "Using plaintext mode on port %s", spice_settings->port);
        g_object_set(spice_session,
                SPICE_PROPERTY_PORT, spice_settings->port, NULL);
    }

    guac_client_log(client, GUAC_LOG_DEBUG, "Finished setting properties.");
    
    /* If connection fails, return NULL */
    return spice_session;

}

void* guac_spice_client_thread(void* data) {
    
    guac_client* client = (guac_client*) data;
    guac_spice_client* spice_client = (guac_spice_client*) client->data;
    guac_spice_settings* settings = spice_client->settings;

    /* Attempt connection */
    guac_client_log(client, GUAC_LOG_DEBUG, "Attempting initial connection to SPICE server.");
    spice_client->spice_session = guac_spice_get_session(client);
    int retries_remaining = settings->retries;

    /* If unsuccessful, retry as many times as specified */
    while (spice_client->spice_session == NULL && retries_remaining > 0) {

        guac_client_log(client, GUAC_LOG_INFO,
                "Connect failed. Waiting %ims before retrying...",
                GUAC_SPICE_CONNECT_INTERVAL);

        /* Wait for given interval then retry */
        guac_timestamp_msleep(GUAC_SPICE_CONNECT_INTERVAL);
        spice_client->spice_session = guac_spice_get_session(client);
        retries_remaining--;

    }

    /* If the final connect attempt fails, return error */
    if (spice_client->spice_session == NULL) {
        guac_client_abort(client, GUAC_PROTOCOL_STATUS_UPSTREAM_NOT_FOUND,
                "Unable to connect to SPICE server.");
        return NULL;
    }
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Configuration completed, flushing socket.");

    guac_socket_flush(client->socket);

    // guac_timestamp last_frame_end = guac_timestamp_current();
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Connection configuration finished, entering main loop.");
    
    if(!spice_session_connect(spice_client->spice_session))
        return NULL;
    
    /* Handle messages from SPICE server while client is running */
    while (client->state == GUAC_CLIENT_RUNNING) {

        /* Wait for an error on the main channel. */
        if (spice_client->main_channel != NULL
                && spice_channel_get_error(SPICE_CHANNEL(spice_client->main_channel)) != NULL)
            break;
    }

    guac_client_log(client, GUAC_LOG_DEBUG, "Exited main loop, cleaning up.");
    
    /* Kill client and finish connection */
    if (spice_client->spice_session != NULL) {
        guac_client_log(client, GUAC_LOG_DEBUG, "Cleaning up SPICE session.");
        spice_session_disconnect(spice_client->spice_session);
        g_object_unref(spice_client->spice_session);
        spice_client->spice_session = NULL;
    }
    guac_client_stop(client);
    guac_client_log(client, GUAC_LOG_INFO, "Internal SPICE client disconnected");
    return NULL;

}
