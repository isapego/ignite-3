/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "ignite/protocol/protocol_version.h"

#include <vector>

namespace ignite::protocol {

/**
 * Make handshake request.
 *
 * @param client_type Client type.
 * @param ver Protocol version.
 * @return Message.
 */
std::vector<std::byte> make_handshake_request(std::int8_t client_type, protocol_version ver);

} // namespace ignite::protocol
