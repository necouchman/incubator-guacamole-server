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

#include "client.h"
#include "common/iconv.h"
#include "common/surface.h"
#include "spice.h"

#include <cairo/cairo.h>
#include <guacamole/client.h>
#include <guacamole/layer.h>
#include <guacamole/protocol.h>
#include <guacamole/socket.h>
#include <spice-client-glib-2.0/spice-client.h>

/* Define cairo_format_stride_for_width() if missing */
#ifndef HAVE_CAIRO_FORMAT_STRIDE_FOR_WIDTH
#define cairo_format_stride_for_width(format, width) (width*4)
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <syslog.h>

void guac_spice_client_display_update(SpiceDisplayChannel* channel, int x,
        int y, int w, int h, guac_client* client) {

    // guac_spice_client* spice_client = (guac_spice_client*) client->data;
    // SpiceDisplayPrimary* primary_display;

    guac_client_log(client, GUAC_LOG_DEBUG, "Calling SPICE client display update.");

}

void guac_spice_client_display_gl_draw(SpiceDisplayChannel* channel, int x,
        int y, int w, int h, guac_client* client) {

    guac_spice_client* spice_client = (guac_spice_client*) client->data;

    /* Copy specified rectangle within default layer */
    guac_common_surface_copy(spice_client->display->default_surface,
            x, y, w, h,
            spice_client->display->default_surface, x, y);
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Channel calling gl_draw.");

}

void guac_spice_client_display_mark(SpiceDisplayChannel* channel, gint mark,
        guac_client* client) {
    
    int channelId;
    
    g_object_get(channel, "channel-id", &channelId, NULL);
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Channel %i marked as available.", channelId);
    
}

void guac_spice_client_display_primary_create(SpiceDisplayChannel* channel,
        gint format, gint width, gint height, gint stride, gint shmid,
        gpointer imgdata, guac_client* client) {
    
    // guac_spice_client* spice_client = (guac_spice_client*) client->data;
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Creating primary display.");
    
}

void guac_spice_client_display_primary_destroy(SpiceDisplayChannel* channel,
        guac_client* client) {
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Destroying primary display.");
    
}
    
void* guac_spice_client_streaming_handler(SpiceDisplayChannel* channel,
        gboolean streaming_mode, guac_client* client) {
    
    guac_spice_client* spice_client = (guac_spice_client*) client->data;
    
    guac_client_log(client, GUAC_LOG_DEBUG, "Calling SPICE streaming handler.");
    
    return spice_client->display;
    
}


