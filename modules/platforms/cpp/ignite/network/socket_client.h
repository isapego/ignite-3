/*
 * Copyright 2019 GridGain Systems, Inc. and Contributors.
 *
 * Licensed under the GridGain Community Edition License (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.gridgain.com/products/software/community-edition/gridgain-community-edition-license
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace ignite::network
{

/**
 * Socket client implementation.
 */
class socket_client
{
public:
    /**
     * Non-negative timeout operation result.
     */
    enum class wait_result
    {
        /** Timeout. */
        TIMEOUT = 0,

        /** Success. */
        SUCCESS = 1
    };

    // Default
    virtual ~socket_client() = default;

    /**
     * Establish connection with remote service.
     *
     * @param hostname Remote host name.
     * @param port Service port.
     * @param timeout Timeout.
     * @return @c true on success and @c false on timeout.
     */
    virtual bool connect(const char* hostname, std::uint16_t port, std::int32_t timeout) = 0;

    /**
     * Close established connection.
     */
    virtual void close() = 0;

    /**
     * Send data by established connection.
     *
     * @param data Pointer to data to be sent.
     * @param size Size of the data in bytes.
     * @param timeout Timeout.
     * @return Number of bytes that have been sent on success,
     *     WaitResult::TIMEOUT on timeout and -errno on failure.
     */
    virtual int send(const std::byte* data, std::size_t size, std::int32_t timeout) = 0;

    /**
     * Receive data from established connection.
     *
     * @param buffer Pointer to data buffer.
     * @param size Size of the buffer in bytes.
     * @param timeout Timeout.
     * @return Number of bytes that have been received on success,
     *     WaitResult::TIMEOUT on timeout and -errno on failure.
     */
    virtual int receive(std::byte* buffer, std::size_t size, std::int32_t timeout) = 0;

    /**
     * Check if the socket is blocking or not.
     *
     * @return @c true if the socket is blocking and false otherwise.
     */
    [[nodiscard]] virtual bool is_blocking() const = 0;
};

} // namespace ignite::network
