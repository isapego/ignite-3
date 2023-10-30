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

#include "big_decimal.h"
#include "big_integer.h"
#include "bit_array.h"
#include "ignite_date.h"
#include "ignite_date_time.h"
#include "ignite_duration.h"
#include "ignite_error.h"
#include "ignite_period.h"
#include "ignite_time.h"
#include "ignite_timestamp.h"
#include "ignite_type.h"
#include "uuid.h"

#include <cstdint>
#include <type_traits>
#include <vector>

#if __cplusplus > 201402L
# include <optional>
# include <variant>
#else
# include "ignite/common/legacy_support.h"
#endif

namespace ignite {

/**
 * Ignite primitive type.
 */
class primitive {
public:
    // Default
    primitive() = default;

    /**
     * Null constructor.
     */
    primitive(std::nullptr_t) {} // NOLINT(google-explicit-constructor)

    /**
     * Null option constructor.
     */
    primitive(std::nullopt_t) {} // NOLINT(google-explicit-constructor)

    /**
     * Constructor for boolean value.
     *
     * @param value Value.
     */
    primitive(bool value) // NOLINT(google-explicit-constructor)
        : m_value(value) {}

    /**
     * Constructor for std::int8_t value.
     *
     * @param value Value.
     */
    primitive(std::int8_t value) // NOLINT(google-explicit-constructor)
        : m_value(value) {}

    /**
     * Constructor for std::int16_t value.
     *
     * @param value Value.
     */
    primitive(std::int16_t value) // NOLINT(google-explicit-constructor)
        : m_value(value) {}

    /**
     * Constructor for std::int32_t value.
     *
     * @param value Value.
     */
    primitive(std::int32_t value) // NOLINT(google-explicit-constructor)
        : m_value(value) {}

    /**
     * Constructor for std::int64_t value.
     *
     * @param value Value.
     */
    primitive(std::int64_t value) // NOLINT(google-explicit-constructor)
        : m_value(value) {}

    /**
     * Constructor for float value.
     *
     * @param value Value.
     */
    primitive(float value) // NOLINT(google-explicit-constructor)
        : m_value(value) {}

    /**
     * Constructor for double value.
     *
     * @param value Value.
     */
    primitive(double value) // NOLINT(google-explicit-constructor)
        : m_value(value) {}

    /**
     * Constructor for UUID value.
     *
     * @param value Value.
     */
    primitive(uuid value) // NOLINT(google-explicit-constructor)
        : m_value(value) {}

    /**
     * Constructor for string value.
     *
     * @param value Value.
     */
    primitive(std::string value) // NOLINT(google-explicit-constructor)
        : m_value(std::move(value)) {}

    /**
     * Constructor for string value.
     *
     * @param value Value.
     */
    primitive(std::string_view value) // NOLINT(google-explicit-constructor)
        : m_value(std::string(value.begin(), value.end())) {}

    /**
     * Constructor for string value.
     *
     * @param value Value.
     */
    primitive(const char *value) // NOLINT(google-explicit-constructor)
        : m_value(std::string(value)) {}

    /**
     * Constructor for byte array value.
     *
     * @param value Value.
     */
    primitive(std::vector<std::byte> value) // NOLINT(google-explicit-constructor)
        : m_value(std::move(value)) {}

    /**
     * Constructor for byte array value.
     *
     * @param buf Buffer.
     * @param len Buffer length.
     */
    primitive(std::byte *buf, std::size_t len)
        : m_value(std::vector<std::byte>(buf, buf + len)) {}

    /**
     * Constructor for big decimal value.
     *
     * @param value Value.
     */
    primitive(big_decimal value) // NOLINT(google-explicit-constructor)
        : m_value(std::move(value)) {}

    /**
     * Constructor for big integer value.
     *
     * @param value Value.
     */
    primitive(big_integer value) // NOLINT(google-explicit-constructor)
        : m_value(std::move(value)) {}

    /**
     * Constructor for date value.
     *
     * @param value Value.
     */
    primitive(ignite_date value) // NOLINT(google-explicit-constructor)
        : m_value(value) {}

    /**
     * Constructor for date-time value.
     *
     * @param value Value.
     */
    primitive(ignite_date_time value) // NOLINT(google-explicit-constructor)
        : m_value(value) {}

    /**
     * Constructor for time value.
     *
     * @param value Value.
     */
    primitive(ignite_time value) // NOLINT(google-explicit-constructor)
        : m_value(value) {}

    /**
     * Constructor for timestamp value.
     *
     * @param value Value.
     */
    primitive(ignite_timestamp value) // NOLINT(google-explicit-constructor)
        : m_value(value) {}

    /**
     * Constructor for period value.
     *
     * @param value Value.
     */
    primitive(ignite_period value) // NOLINT(google-explicit-constructor)
        : m_value(value) {}

    /**
     * Constructor for duration value.
     *
     * @param value Value.
     */
    primitive(ignite_duration value) // NOLINT(google-explicit-constructor)
        : m_value(value) {}

    /**
     * Constructor for bitmask value.
     *
     * @param value Value.
     */
    primitive(bit_array value) // NOLINT(google-explicit-constructor)
        : m_value(std::move(value)) {}

    /**
     * Get underlying value.
     *
     * @tparam T Type of value to try and get.
     * @return Value of the specified type.
     * @throw ignite_error if primitive contains value of any other type.
     */
    template<typename T>
    [[nodiscard]] const T &get() const {
        return std::get<T>(m_value);
    }

    /**
     * Check whether element is null.
     *
     * @return Value indicating whether element is null.
     */
    [[nodiscard]] bool is_null() const noexcept { return m_value.index() == 0; }

    /**
     * Get primitive type.
     *
     * @return Primitive type.
     */
    [[nodiscard]] ignite_type get_type() const noexcept {
        if (is_null())
            return ignite_type::UNDEFINED;
        return static_cast<ignite_type>(m_value.index() - 1);
    }

    /**
     * @brief Comparison operator.
     *
     * @param lhs First value.
     * @param rhs Second value.
     * @return true If values are equal.
     */
    friend bool operator==(const primitive &lhs, const primitive &rhs) noexcept {
        return lhs.m_value == rhs.m_value;
    }

    /**
     * @brief Comparison operator.
     *
     * @param lhs First value.
     * @param rhs Second value.
     * @return true If values are not equal.
     */
    friend bool operator!=(const primitive &lhs, const primitive &rhs) noexcept {
        return lhs.m_value != rhs.m_value;
    }

private:
    /** Value type. */
    typedef std::variant<std::nullptr_t,
        bool, // Bool = 0
        std::int8_t, // Int8 = 1
        std::int16_t, // Int16 = 2
        std::int32_t, // Int32 = 3
        std::int64_t, // Int64 = 4
        float, // Float = 5
        double, // Double = 6
        big_decimal, // Decimal = 7
        ignite_date, // Date = 8
        ignite_time, // Time = 9
        ignite_date_time, // Datetime = 10
        ignite_timestamp, // Timestamp = 11
        uuid, // UUID = 12
        bit_array, // Bitmask = 13
        std::string, // String = 14
        std::vector<std::byte>, // Bytes = 15
        ignite_period, // Period = 16
        ignite_duration, // Duration = 17
        big_integer // Big Integer = 18
        >
        value_type;

    /** Value. */
    value_type m_value;
};

} // namespace ignite
