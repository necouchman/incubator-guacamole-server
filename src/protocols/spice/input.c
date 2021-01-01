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

#include "common/cursor.h"
#include "common/display.h"
#include "common/recording.h"
#include "spice.h"
#include "spice-constants.h"

#include <guacamole/user.h>
#include <spice-client-glib-2.0/spice-client.h>

int guac_spice_user_mouse_handler(guac_user* user, int x, int y, int mask) {

    guac_client* client = user->client;
    guac_spice_client* spice_client = (guac_spice_client*) client->data;
    // SpiceSession* spice_session = spice_client->spice_session;

    guac_user_log(user, GUAC_LOG_TRACE, "Handling mouse event.");
    
    /* Store current mouse location/state */
    guac_common_cursor_update(spice_client->display->cursor, user, x, y, mask);

    /* Report mouse position within recording */
    if (spice_client->recording != NULL)
        guac_common_recording_report_mouse(spice_client->recording, x, y, mask);

    /* Send SPICE event only if finished connecting */

    return 0;
}

int guac_spice_user_key_handler(guac_user* user, int keysym, int pressed) {
    
    guac_spice_client* spice_client = (guac_spice_client*) user->client->data;
    // SpiceSession* spice_session = spice_client->spice_session;

    guac_user_log(user, GUAC_LOG_TRACE, "Handling keypress.");
    
    /* Report key state within recording */
    if (spice_client->recording != NULL)
        guac_common_recording_report_key(spice_client->recording,
                keysym, pressed);

    /* Send SPICE event only if finished connecting */


    return 0;
}

void guac_spice_mouse_mode_update(SpiceChannel* channel, guac_client* client) {
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Updating mouse mode.");
    
}

void guac_spice_inputs_modifiers(SpiceChannel* channel, guac_client* client) {
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Received inputs modifiers signal.");
    
    int modifiers;
    
    g_object_get(channel, SPICE_PROPERTY_KEY_MODIFIERS, &modifiers, NULL);
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Key modifiers: %i", modifiers);
    
}