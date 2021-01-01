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
#include "common/recording.h"
#include "client.h"
#include "channels/audio.h"
#include "channels/cursor.h"
#include "channels/display.h"
#include "channels/file.h"
#include "input.h"
#include "user.h"
#include "spice.h"
#include "spice-constants.h"

#ifdef ENABLE_COMMON_SSH
#include "common-ssh/sftp.h"
#include "common-ssh/ssh.h"
#include "common-ssh/user.h"
#endif

#include <guacamole/client.h>

#include <pthread.h>
#include <spice-client-glib-2.0/spice-client.h>
#include <stdlib.h>
#include <string.h>

/**
 * Handle events for the main SPICE channel, taking the appropriate action
 * for known events, and logging warnings for unknowns and non-fatal events.
 * 
 * @param channel
 *     The channel for which to handle events.
 * 
 * @param event
 *     The event that is being handled.
 * 
 * @param client
 *     The guacamole_client associated with this event.
 */
static void guac_spice_client_main_channel_handler(SpiceChannel *channel,
        SpiceChannelEvent event, guac_client* client) {
    
    guac_spice_client* spice_client = (guac_spice_client*) client->data;
    guac_client_log(client, GUAC_LOG_DEBUG, "Received new main channel event: %u", event);
    
    /* Handle the various possible SPICE events. */
    switch (event) {
        
        /* Channel has been closed, so we abort the connection. */
        case SPICE_CHANNEL_CLOSED:
            guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR,
                    "Disconnected from SPICE server.");
            break;
            
        /* Channel has been opened - log it and do nothing else. */
        case SPICE_CHANNEL_OPENED:
            guac_client_log(client, GUAC_LOG_DEBUG, "Channel opened.");
            break;
            
        /* Error authenticating, log a warning and prompt user. */
        case SPICE_CHANNEL_ERROR_AUTH:
            guac_client_log(client, GUAC_LOG_WARNING,
                    "Channel authentication failed.");
            
            if (guac_spice_get_credentials(client) 
                    && spice_session_open_fd(spice_client->spice_session, -1))
                guac_client_log(client, GUAC_LOG_DEBUG, "Session connection started.");
            
            else
                guac_client_abort(client,
                        GUAC_PROTOCOL_STATUS_CLIENT_UNAUTHORIZED,
                        "Failed to get credentials to connect to server.");
            
            break;
            
        /* TLS error, abort the connection with a warning. */
        case SPICE_CHANNEL_ERROR_TLS:
            guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR,
                    "TLS failure connecting to SPICE server.");
            break;
            
        /* I/O error, abort the connection with a warning. */
        case SPICE_CHANNEL_ERROR_IO:
            guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR,
                    "IO error communicating with SPICE server.");
            break;
            
        /* SPICE link error, abort the connection with a warning. */
        case SPICE_CHANNEL_ERROR_LINK:
            guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR,
                    "Link error communicating with SPICE server.");
            break;
            
        /* Connect error, abort the connection with a warning. */
        case SPICE_CHANNEL_ERROR_CONNECT:
            guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR,
                    "Connection error communicating with SPICe server.");
            break;
            
        /* Some other unknown event - log it and move on. */
        default:
            guac_client_log(client, GUAC_LOG_WARNING,
                    "Unknown event received on channel.");
    }
    
}

/**
 * Callback that is invoked when the SPICE client receives an event indicating
 * that the agent within the SPICE server is connected, allowing us to take
 * some action based on that connection.
 * 
 * @param channel
 *     The channel that received the event.
 * 
 * @param client
 *     The guacamole_client object associated with this event.
 */
static void guac_spice_client_agent_connected_handler(SpiceChannel *channel,
        guac_client* client) {
    
    gboolean connected;
    
    g_object_get(channel, SPICE_PROPERTY_AGENT_CONNECTED, &connected, NULL);
    
    if (connected)
        guac_client_log(client, GUAC_LOG_DEBUG, "SPICE agent connected.");
    else
        guac_client_log(client, GUAC_LOG_DEBUG, "SPICE agent not connected.");
    
}

int guac_client_init(guac_client* client) {

    /* Set client args */
    client->args = GUAC_SPICE_CLIENT_ARGS;

    /* Alloc client data */
    guac_spice_client* spice_client = calloc(1, sizeof(guac_spice_client));
    client->data = spice_client;

    /* Init clipboard */
    spice_client->clipboard =
            guac_common_clipboard_alloc(GUAC_SPICE_CLIPBOARD_MAX_LENGTH);

    /* Set handlers */
    client->join_handler = guac_spice_user_join_handler;
    client->leave_handler = guac_spice_user_leave_handler;
    client->free_handler = guac_spice_client_free_handler;

    return 0;
}

int guac_spice_client_free_handler(guac_client* client) {

    guac_spice_client* spice_client = (guac_spice_client*) client->data;
    guac_spice_settings* settings = spice_client->settings;

    /* Clean up SPICE client*/
    SpiceSession* spice_session = spice_client->spice_session;
    if (spice_session != NULL) {

        /* Wait for client thread to finish */
        pthread_join(spice_client->client_thread, NULL);
        
        /* Disconnect the session, destroying data */
        spice_session_disconnect(spice_session);

    }

#ifdef ENABLE_COMMON_SSH
    /* Free SFTP filesystem, if loaded */
    if (spice_client->sftp_filesystem)
        guac_common_ssh_destroy_sftp_filesystem(spice_client->sftp_filesystem);

    /* Free SFTP session */
    if (spice_client->sftp_session)
        guac_common_ssh_destroy_session(spice_client->sftp_session);

    /* Free SFTP user */
    if (spice_client->sftp_user)
        guac_common_ssh_destroy_user(spice_client->sftp_user);

    guac_common_ssh_uninit();
#endif

    /* Clean up recording, if in progress */
    if (spice_client->recording != NULL)
        guac_common_recording_free(spice_client->recording);

    /* Free clipboard */
    if (spice_client->clipboard != NULL)
        guac_common_clipboard_free(spice_client->clipboard);

    /* Free display */
    if (spice_client->display != NULL)
        guac_common_display_free(spice_client->display);

    /* Free parsed settings */
    if (settings != NULL)
        guac_spice_settings_free(settings);

    /* Free generic data struct */
    free(client->data);

    return 0;
}

static void guac_spice_client_open_fd_handler(SpiceChannel* channel,
        gint with_tls, guac_client* client) {
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Received open-fd event for channel.");
    
}

void guac_spice_client_channel_handler(SpiceSession *spice_session,
        SpiceChannel *channel, guac_client* client) {
    
    guac_spice_client* spice_client = (guac_spice_client*) client->data;
    guac_spice_settings* settings = spice_client->settings;
    int id;
    
    /* Get the channel ID */
    g_object_get(channel, SPICE_PROPERTY_CHANNEL_ID, &id, NULL);
    
    guac_client_log(client, GUAC_LOG_DEBUG, "New channel created: %i", id);
    
    g_signal_connect(channel, SPICE_SIGNAL_CHANNEL_OPEN_FD,
            G_CALLBACK(guac_spice_client_open_fd_handler), client);
    
    /* Check if this is the main channel and register handlers */
    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        guac_client_log(client, GUAC_LOG_DEBUG, "Setting up main channel.");
        spice_client->main_channel = SPICE_MAIN_CHANNEL(channel);
        g_signal_connect(channel, SPICE_SIGNAL_CHANNEL_EVENT,
                G_CALLBACK(guac_spice_client_main_channel_handler), client);
        g_signal_connect(channel, SPICE_SIGNAL_MAIN_AGENT_UPDATE,
                G_CALLBACK(guac_spice_client_agent_connected_handler), client);
        g_signal_connect(channel, SPICE_SIGNAL_NEW_FILE_TRANSFER,
                G_CALLBACK(guac_spice_client_file_transfer_handler), client);
        g_signal_connect(channel, SPICE_SIGNAL_MAIN_MOUSE_UPDATE,
                G_CALLBACK(guac_spice_mouse_mode_update), client);
        guac_client_log(client, GUAC_LOG_DEBUG, "Main channel configuration completed.");
    }
    
    /* Check if this is the display channel and register display handlers. */
    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        guac_client_log(client, GUAC_LOG_DEBUG, "Setting up display channel.");
        int width, height;
        SpiceDisplayPrimary primary;
        g_object_get(channel, "width", &width, "height", &height, NULL);
        spice_client->spice_display = SPICE_DISPLAY_CHANNEL(channel);
        spice_client->display = guac_common_display_alloc(client, width, height);
        g_signal_connect(channel, SPICE_SIGNAL_DISPLAY_INVALIDATE,
                G_CALLBACK(guac_spice_client_display_update), client);
        g_signal_connect(channel, SPICE_SIGNAL_DISPLAY_MARK,
                G_CALLBACK(guac_spice_client_display_mark), client);
        g_signal_connect(channel, SPICE_SIGNAL_DISPLAY_PRIMARY_CREATE,
                G_CALLBACK(guac_spice_client_display_primary_create), client);
        g_signal_connect(channel, SPICE_SIGNAL_DISPLAY_PRIMARY_DESTROY,
                G_CALLBACK(guac_spice_client_display_primary_destroy), client);
        g_signal_connect(channel, SPICE_SIGNAL_GL_DRAW,
                G_CALLBACK(guac_spice_client_display_gl_draw), client);
        g_signal_connect(channel, SPICE_SIGNAL_STREAMING_MODE,
                G_CALLBACK(guac_spice_client_streaming_handler), client);
        
        /* Attempt to get the primary display, and set it up. */
        if (spice_display_channel_get_primary(channel, 0, &primary)) {
            guac_spice_client_display_primary_create(
                    spice_client->spice_display, primary.format,
                    primary.width, primary.height, primary.stride,
                    primary.shmid, primary.data, client);
            guac_spice_client_display_mark(spice_client->spice_display,
                    primary.marked, client);
            spice_client->spice_display_primary = &primary;
        }
        
        if (!spice_channel_connect(channel))
            guac_client_abort(client, GUAC_PROTOCOL_STATUS_SERVER_ERROR,
                    "Unable to connect the display channel.");
        
    }
    
    /* Check for audio playback channel and set up the channel. */
    if (SPICE_IS_PLAYBACK_CHANNEL(channel) && settings->audio_enabled) {
        guac_client_log(client, GUAC_LOG_DEBUG, "Setting up audio playback channel.");
        spice_client->playback_channel = SPICE_PLAYBACK_CHANNEL(channel);
        g_signal_connect(channel, SPICE_SIGNAL_PLAYBACK_DATA,
                G_CALLBACK(guac_spice_client_audio_playback_data_handler), client);
        g_signal_connect(channel, SPICE_SIGNAL_PLAYBACK_GET_DELAY,
                G_CALLBACK(guac_spice_client_audio_playback_delay_handler), client);
        g_signal_connect(channel, SPICE_SIGNAL_PLAYBACK_START,
                G_CALLBACK(guac_spice_client_audio_playback_start_handler), client);
        g_signal_connect(channel, SPICE_SIGNAL_PLAYBACK_STOP,
                G_CALLBACK(guac_spice_client_audio_playback_stop_handler), client);
    }
    
    /* Check for audio recording channel and set up the channel. */
    if (SPICE_IS_RECORD_CHANNEL(channel) && settings->audio_enabled) {
        guac_client_log(client, GUAC_LOG_DEBUG, "Setting up audio record channel.");
        spice_client->record_channel = SPICE_RECORD_CHANNEL(channel);
        g_signal_connect(channel, SPICE_SIGNAL_RECORD_START,
                G_CALLBACK(guac_spice_client_audio_record_start_handler), client);
        g_signal_connect(channel, SPICE_SIGNAL_RECORD_STOP,
                G_CALLBACK(guac_spice_client_audio_record_stop_handler), client);
    }
    
    /* Check for cursor channel and set it up. */
    if (SPICE_IS_CURSOR_CHANNEL(channel)) {
        guac_client_log(client, GUAC_LOG_DEBUG, "Setting up cursor channel.");
        spice_client->cursor_channel = SPICE_CURSOR_CHANNEL(channel);
        g_signal_connect(channel, SPICE_SIGNAL_CURSOR_HIDE,
                G_CALLBACK(guac_spice_cursor_hide), client);
        g_signal_connect(channel, SPICE_SIGNAL_CURSOR_MOVE,
                G_CALLBACK(guac_spice_cursor_move), client);
        g_signal_connect(channel, SPICE_SIGNAL_CURSOR_RESET,
                G_CALLBACK(guac_spice_cursor_reset), client);
        g_signal_connect(channel, SPICE_SIGNAL_CURSOR_SET,
                G_CALLBACK(guac_spice_cursor_set), client);
    }
    
    /* Check if this is an inputs channel and set it up. */
    if (SPICE_IS_INPUTS_CHANNEL(channel)) {
        guac_client_log(client, GUAC_LOG_DEBUG, "Setting up inputs channel.");
        spice_client->inputs_channel = SPICE_INPUTS_CHANNEL(channel);
        g_signal_connect(channel, SPICE_SIGNAL_INPUTS_MODIFIERS,
                G_CALLBACK(guac_spice_inputs_modifiers), client);
    }
    
    /* File transfer channel */
    if (SPICE_IS_WEBDAV_CHANNEL(channel)) {
        guac_client_log(client, GUAC_LOG_DEBUG, "Setting up webdav channel.");
        if (settings->file_transfer
                && settings->file_directory != NULL
                && strcmp(settings->file_directory, "") != 0) {
            spice_channel_connect(channel);
        }
    }
    
    spice_channel_connect(channel);
    
}
