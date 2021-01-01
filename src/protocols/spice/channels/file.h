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


#ifndef GUAC_SPICE_FILE_H
#define GUAC_SPICE_FILE_H

#include "config.h"

#include <guacamole/client.h>

#include <spice-client-glib-2.0/spice-client.h>

/**
 * A handler that is called when the SPICE client receives notification of 
 * a new file transfer task.
 * 
 * @param main_channel
 *     The main channel associated with the SPICE session.
 * 
 * @param task
 *     The file transfer task that triggered this function call.
 * 
 * @param client
 *     The guac_client object associated with this session.
 */
void guac_spice_client_file_transfer_handler(SpiceMainChannel* main_channel,
        SpiceFileTransferTask* task, guac_client* client);

#endif /* GUAC_SPICE_FILE_H */

