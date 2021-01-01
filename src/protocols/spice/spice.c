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

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

SpiceSession* guac_spice_get_session(guac_client* client) {

    guac_client_log(client, GUAC_LOG_DEBUG, "Initializing new SPICE session.");
    
    /* Set up the SPICE session and Guacamole client. */
    guac_spice_client* spice_client = (guac_spice_client*) client->data;
    guac_spice_settings* spice_settings = spice_client->settings;
    
    /* Create a new SPICE client. */
    SpiceSession* spice_session = spice_session_new();
    spice_set_session_option(spice_session);
    
    /* Associate the SPICE session with the Guacamole Client data. */
    g_object_set_data_full(G_OBJECT(spice_session),
            GUAC_SPICE_CLIENT_KEY, client, g_free);
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Registering main channel callback.");
    
    /* Register a callback for handling new channel events. */
    g_signal_connect(spice_session, SPICE_SIGNAL_CHANNEL_NEW,
            G_CALLBACK(guac_spice_client_channel_handler), client);
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Setting up connection properties.");
    
    /* Do not handle clipboard and local cursor if read-only */
    g_object_set(G_OBJECT(spice_session),
            SPICE_PROPERTY_READ_ONLY, spice_settings->read_only, NULL);
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Setting up authentication parameters.");
    
    /* Set authentication data */
    /*
    if (spice_settings->username != NULL)
        g_object_set(spice_session,
                SPICE_PROPERTY_USERNAME, spice_settings->username, NULL);
    if (spice_settings->password != NULL)
        g_object_set(spice_session,
                SPICE_PROPERTY_PASSWORD, spice_settings->password, NULL);

    */
    guac_client_log(client, GUAC_LOG_DEBUG, "Setting up host/port.");
    
    /* Set hostname and port */
    g_object_set(spice_session, SPICE_PROPERTY_HOST, spice_settings->hostname, NULL);
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
    
    /* Set the proxy server if specified. */
    if (spice_settings->proxy != NULL)
        g_object_set(spice_session,
                SPICE_PROPERTY_PROXY, spice_settings->proxy, NULL);
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Setting color depth.");
    
    /* Set color depth */
    if (spice_settings->color_depth > 0)
        g_object_set(spice_session,
                SPICE_PROPERTY_COLOR_DEPTH, spice_settings->color_depth, NULL);
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Setting up file transfers.");
    
    /* Set up file transfer directory. */
    if (spice_settings->file_transfer && spice_settings->file_directory != NULL)
        g_object_set(spice_session,
                SPICE_PROPERTY_SHARED_DIR, spice_settings->file_directory,
                SPICE_PROPERTY_SHARED_DIR_RO, spice_settings->file_transfer_ro,
                NULL);

    guac_client_log(client, GUAC_LOG_DEBUG, "Finished setting properties.");
    
    /* If connection fails, return NULL */
    return spice_session;

}

void* guac_spice_client_thread(void* data) {
    
    spice_util_set_debug(TRUE);
    
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

#ifdef ENABLE_COMMON_SSH
    guac_client_log(client, GUAC_LOG_DEBUG, "Initializing SFTP and looking for configuration.");
    guac_common_ssh_init(client);

    /* Connect via SSH if SFTP is enabled */
    if (settings->enable_sftp) {

        guac_client_log(client, GUAC_LOG_DEBUG, "SFTP enabled, setting up connection.");
        
        /* Abort if username is missing */
        if (settings->sftp_username == NULL) {
            guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR,
                    "SFTP username is required if SFTP is enabled.");
            return NULL;
        }

        guac_client_log(client, GUAC_LOG_DEBUG,
                "Connecting via SSH for SFTP filesystem access.");

        spice_client->sftp_user =
            guac_common_ssh_create_user(settings->sftp_username);

        /* Import private key, if given */
        if (settings->sftp_private_key != NULL) {

            guac_client_log(client, GUAC_LOG_DEBUG,
                    "Authenticating with private key.");

            /* Abort if private key cannot be read */
            if (guac_common_ssh_user_import_key(spice_client->sftp_user,
                        settings->sftp_private_key,
                        settings->sftp_passphrase)) {
                guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR,
                        "Private key unreadable.");
                return NULL;
            }

        }

        /* Otherwise, use specified password */
        else {
            guac_client_log(client, GUAC_LOG_DEBUG,
                    "Authenticating with password.");
            guac_common_ssh_user_set_password(spice_client->sftp_user,
                    settings->sftp_password);
        }

        /* Attempt SSH connection */
        spice_client->sftp_session =
            guac_common_ssh_create_session(client, settings->sftp_hostname,
                    settings->sftp_port, spice_client->sftp_user, settings->sftp_server_alive_interval,
                    settings->sftp_host_key, NULL);

        /* Fail if SSH connection does not succeed */
        if (spice_client->sftp_session == NULL) {
            /* Already aborted within guac_common_ssh_create_session() */
            return NULL;
        }

        /* Load filesystem */
        spice_client->sftp_filesystem =
            guac_common_ssh_create_sftp_filesystem(spice_client->sftp_session,
                    settings->sftp_root_directory, NULL,
                    settings->sftp_disable_download,
                    settings->sftp_disable_upload);

        /* Expose filesystem to connection owner */
        guac_client_for_owner(client,
                guac_common_ssh_expose_sftp_filesystem,
                spice_client->sftp_filesystem);

        /* Abort if SFTP connection fails */
        if (spice_client->sftp_filesystem == NULL) {
            guac_client_abort(client, GUAC_PROTOCOL_STATUS_UPSTREAM_ERROR,
                    "SFTP connection failed.");
            return NULL;
        }

        /* Configure destination for basic uploads, if specified */
        if (settings->sftp_directory != NULL)
            guac_common_ssh_sftp_set_upload_path(
                    spice_client->sftp_filesystem,
                    settings->sftp_directory);

        guac_client_log(client, GUAC_LOG_DEBUG,
                "SFTP connection succeeded.");

    }
#endif

    guac_client_log(client, GUAC_LOG_DEBUG, "SFTP finished, setting up remaining configuration.");
    
    /* Set remaining client data */
    

    /* Set up screen recording, if requested */
    if (settings->recording_path != NULL) {
        guac_client_log(client, GUAC_LOG_WARNING, "Session recording is enabled, creating the recording.");
        spice_client->recording = guac_common_recording_create(client,
                settings->recording_path,
                settings->recording_name,
                settings->create_recording_path,
                !settings->recording_exclude_output,
                !settings->recording_exclude_mouse,
                settings->recording_include_keys);
    }

    /* If not read-only, set an appropriate cursor */
    /*
    if (settings->read_only == 0) {
        guac_client_log(client, GUAC_LOG_DEBUG, "Connection is not read-only, setting up input handlers.");
        if (settings->remote_cursor)
            guac_common_cursor_set_dot(spice_client->display->cursor);
        else
            guac_common_cursor_set_pointer(spice_client->display->cursor);

    }
    */
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Configuration completed, flushing socket.");

    guac_socket_flush(client->socket);

    // guac_timestamp last_frame_end = guac_timestamp_current();
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Connection configuration finished, entering main loop.");
    
    // Set up the socket
    int fd;
    int retval;
    struct addrinfo* addresses;
    struct addrinfo* current_address;
    
    char connected_address[1024];
    char connected_port[64];
    
    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP
    };

    /* Get socket */
    fd = socket(AF_INET, SOCK_STREAM, 0);
    
    /* Get addresses connection */
    if ((retval = getaddrinfo(settings->hostname, settings->port,
                    &hints, &addresses))) {
        guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR, "Error parsing given address or port: %s",
                gai_strerror(retval));
        return NULL;

    }

    /* Attempt connection to each address until success */
    current_address = addresses;
    while (current_address != NULL) {

        int retval;

        /* Resolve hostname */
        if ((retval = getnameinfo(current_address->ai_addr,
                current_address->ai_addrlen,
                connected_address, sizeof(connected_address),
                connected_port, sizeof(connected_port),
                NI_NUMERICHOST | NI_NUMERICSERV)))
            guac_client_log(client, GUAC_LOG_DEBUG, "Unable to resolve host: %s", gai_strerror(retval));

        /* Connect */
        if (connect(fd, current_address->ai_addr,
                        current_address->ai_addrlen) == 0) {

            guac_client_log(client, GUAC_LOG_DEBUG, "Successfully connected to "
                    "host %s, port %s", connected_address, connected_port);

            /* Done if successful connect */
            break;

        }

        /* Otherwise log information regarding bind failure */
        else
            guac_client_log(client, GUAC_LOG_DEBUG, "Unable to connect to "
                    "host %s, port %s: %s",
                    connected_address, connected_port, strerror(errno));

        current_address = current_address->ai_next;

    }
    
    /* If unable to connect to anything, fail */
    if (current_address == NULL) {
        guac_client_abort(client, GUAC_PROTOCOL_STATUS_UPSTREAM_NOT_FOUND,
                "Unable to connect to any addresses.");
        return NULL;
    }

    /* Free addrinfo */
    freeaddrinfo(addresses);
    
    // Try SPICE session on the fd.
    if(!spice_session_open_fd(spice_client->spice_session, fd))
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
