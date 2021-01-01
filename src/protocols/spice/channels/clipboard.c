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
#include "clipboard.h"
#include "common/clipboard.h"
#include "common/iconv.h"
#include "spice.h"
#include "spice-constants.h"
#include "user.h"

#include <guacamole/client.h>
#include <guacamole/stream.h>
#include <guacamole/user.h>

int guac_spice_set_clipboard_encoding(guac_client* client,
        const char* name) {

    guac_spice_client* spice_client = (guac_spice_client*) client->data;

    /* Use ISO8859-1 if explicitly selected or NULL */
    if (name == NULL || strcmp(name, "ISO8859-1") == 0) {
        spice_client->clipboard_reader = GUAC_READ_ISO8859_1;
        spice_client->clipboard_writer = GUAC_WRITE_ISO8859_1;
        return 0;
    }

    /* UTF-8 */
    if (strcmp(name, "UTF-8") == 0) {
        spice_client->clipboard_reader = GUAC_READ_UTF8;
        spice_client->clipboard_writer = GUAC_WRITE_UTF8;
        return 1;
    }

    /* UTF-16 */
    if (strcmp(name, "UTF-16") == 0) {
        spice_client->clipboard_reader = GUAC_READ_UTF16;
        spice_client->clipboard_writer = GUAC_WRITE_UTF16;
        return 1;
    }

    /* CP1252 */
    if (strcmp(name, "CP1252") == 0) {
        spice_client->clipboard_reader = GUAC_READ_CP1252;
        spice_client->clipboard_writer = GUAC_WRITE_CP1252;
        return 1;
    }

    /* If encoding unrecognized, warn and default to ISO8859-1 */
    guac_client_log(client, GUAC_LOG_WARNING,
            "Encoding '%s' is invalid. Defaulting to ISO8859-1.", name);

    spice_client->clipboard_reader = GUAC_READ_ISO8859_1;
    spice_client->clipboard_writer = GUAC_WRITE_ISO8859_1;
    return 0;

}

int guac_spice_clipboard_handler(guac_user* user, guac_stream* stream,
        char* mimetype) {

    /* Clear clipboard and prepare for new data */
    guac_spice_client* spice_client = (guac_spice_client*) user->client->data;
    guac_common_clipboard_reset(spice_client->clipboard, mimetype);

    /* Set handlers for clipboard stream */
    stream->blob_handler = guac_spice_clipboard_blob_handler;
    stream->end_handler = guac_spice_clipboard_end_handler;

    return 0;
}

int guac_spice_clipboard_blob_handler(guac_user* user, guac_stream* stream,
        void* data, int length) {

    /* Append new data */
    guac_spice_client* spice_client = (guac_spice_client*) user->client->data;
    guac_common_clipboard_append(spice_client->clipboard, (char*) data, length);

    return 0;
}

int guac_spice_clipboard_end_handler(guac_user* user, guac_stream* stream) {

    guac_spice_client* spice_client = (guac_spice_client*) user->client->data;
    // SpiceSession* spice_session = spice_client->spice_session;

    char output_data[GUAC_SPICE_CLIPBOARD_MAX_LENGTH];

    const char* input = spice_client->clipboard->buffer;
    char* output = output_data;
    guac_iconv_write* writer = spice_client->clipboard_writer;

    /* Convert clipboard contents */
    guac_iconv(GUAC_READ_UTF8, &input, spice_client->clipboard->length,
               writer, &output, sizeof(output_data));

    /* Send via SPICE only if finished connecting */
    /*
    if (spice_session != NULL)
        SendClientCutText(spice_session, output_data, output - output_data);
     */

    return 0;
}

void guac_spice_cut_text(SpiceSession* spice_session, const char* text, int textlen) {

    guac_client* gc = g_object_get_data(G_OBJECT(spice_session), GUAC_SPICE_CLIENT_KEY);
    guac_spice_client* spice_client = (guac_spice_client*) gc->data;

    /* Ignore received text if outbound clipboard transfer is disabled */
    if (spice_client->settings->disable_copy)
        return;

    char received_data[GUAC_SPICE_CLIPBOARD_MAX_LENGTH];

    const char* input = text;
    char* output = received_data;
    guac_iconv_read* reader = spice_client->clipboard_reader;

    /* Convert clipboard contents */
    guac_iconv(reader, &input, textlen,
               GUAC_WRITE_UTF8, &output, sizeof(received_data));

    /* Send converted data */
    guac_common_clipboard_reset(spice_client->clipboard, "text/plain");
    guac_common_clipboard_append(spice_client->clipboard, received_data, output - received_data);
    guac_common_clipboard_send(spice_client->clipboard, gc);

}

