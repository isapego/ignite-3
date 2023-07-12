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

#ifdef _WIN32
#   include <windows.h>
#endif

#include "odbc_test_utils.h"
#include "test_utils.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string_view>
#include <utility>
#include <thread>

#include <sql.h>
#include <sqlext.h>

namespace ignite {

/**
 * Test suite.
 */
class odbc_connection {
public:
    /**
     * Destructor.
     */
    ~odbc_connection() {
        odbc_clean_up();
    }

    /**
     * Prepare handles for connection.
     */
    void prepare_environment() {
        ignite::prepare_environment(m_env, m_conn);
    }

    /**
     * ODBC connect.
     *
     * @param connect_str Connect string.
     */
    void odbc_connect(std::string_view connect_str) {
        ignite::odbc_connect(connect_str, m_env, m_conn, m_statement);
    }

    /**
     * Disconnect.
     */
    void odbc_disconnect() {
        ignite::odbc_disconnect(m_conn, m_statement);
    }

    /**
     * Clean up handles.
     */
    void odbc_clean_up() {
        ignite::odbc_clean_up(m_env, m_conn, m_statement);
    }

    /**
     * Execute query.
     *
     * @param qry Query.
     * @return Result.
     */
    SQLRETURN exec_query(const std::string& qry) { // NOLINT(readability-make-member-function-const)
        auto sql = to_sqlchar(qry);
        return SQLExecDirect(m_statement, sql.data(), static_cast<SQLINTEGER>(sql.size()));
    }

    /**
     * Get statement error state.
     *
     * @return Statement error state.
     */
    [[nodiscard]] std::string get_statement_error_state() const {
        return get_odbc_error_state(SQL_HANDLE_STMT, m_statement);
    }

    /**
     * Get statement error message.
     *
     * @return Statement error message.
     */
    [[nodiscard]] std::string get_statement_error_message() const {
        return get_odbc_error_message(SQL_HANDLE_STMT, m_statement);
    }

    /**
     * Get connection error state.
     *
     * @return Connection error state.
     */
    [[nodiscard]] std::string get_connection_error_state() const {
        return get_odbc_error_state(SQL_HANDLE_DBC, m_conn);
    }

    /**
     * Wait for table to become available.
     *
     * @param table Table name to wait.
     * @param timeout Timeout.
     * @return @c true if table is available, @c false on timeout.
     */
    bool wait_for_table(const std::string &table, std::chrono::seconds timeout) {
        auto start_time = std::chrono::steady_clock::now();
        do {
            auto res = exec_query("select * from " + table);
            if (SQL_SUCCEEDED(res))
                return true;

            std::this_thread::sleep_for(std::chrono::seconds(1));
        } while ((std::chrono::steady_clock::now() - start_time) < timeout);
        return false;
    }

    /**
     * Insert test strings.
     *
     * @param records_num Number of strings to insert.
     * @param merge 
     */
    void insert_test_strings(SQLSMALLINT records_num, bool merge) // NOLINT(readability-make-member-function-const)
    {
        SQLCHAR insert_req[] = "INSERT INTO TBL_ALL_COLUMNS_SQL(key, str) VALUES(?, ?)";
        SQLCHAR merge_req[] = "MERGE INTO TBL_ALL_COLUMNS_SQL(key, str) VALUES(?, ?)";
        
        SQLRETURN ret = SQLPrepare(m_statement, merge ? merge_req : insert_req, SQL_NTS);

        if (!SQL_SUCCEEDED(ret))
            FAIL() << (get_odbc_error_message(SQL_HANDLE_STMT, m_statement));

        std::int64_t key = 0;
        char str_field[1024] = {0};
        SQLLEN str_field_len = 0;

        // Binding parameters.
        ret = SQLBindParameter(m_statement, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_BIGINT, 0, 0, &key, 0, nullptr);

        if (!SQL_SUCCEEDED(ret))
            FAIL() << (get_odbc_error_message(SQL_HANDLE_STMT, m_statement));

        ret = SQLBindParameter(m_statement, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, sizeof(str_field),
            sizeof(str_field), &str_field, sizeof(str_field), &str_field_len);

        if (!SQL_SUCCEEDED(ret))
            FAIL() << (get_odbc_error_message(SQL_HANDLE_STMT, m_statement));

        // Inserting values.
        for (SQLSMALLINT i = 0; i < records_num; ++i)
        {
            key = i + 1;
            std::string val = "String#" + std::to_string(i);

            strncpy(str_field, val.c_str(), sizeof(str_field) - 1);
            str_field_len = SQL_NTS;

            ret = SQLExecute(m_statement);

            if (!SQL_SUCCEEDED(ret))
                FAIL() << (get_odbc_error_message(SQL_HANDLE_STMT, m_statement));

            SQLLEN affected = 0;
            ret = SQLRowCount(m_statement, &affected);

            if (!SQL_SUCCEEDED(ret))
                FAIL() << (get_odbc_error_message(SQL_HANDLE_STMT, m_statement));

            EXPECT_EQ(affected, 1);

            ret = SQLMoreResults(m_statement);

            if (ret != SQL_NO_DATA)
                FAIL() << (get_odbc_error_message(SQL_HANDLE_STMT, m_statement));
        }

        // Resetting parameters.
        ret = SQLFreeStmt(m_statement, SQL_RESET_PARAMS);

        if (!SQL_SUCCEEDED(ret))
            FAIL() << (get_odbc_error_message(SQL_HANDLE_STMT, m_statement));
    }

    /** Environment handle. */
    SQLHENV m_env{SQL_NULL_HANDLE};

    /** Connection handle. */
    SQLHDBC m_conn{SQL_NULL_HANDLE};

    /** Statement handle. */
    SQLHSTMT m_statement{SQL_NULL_HANDLE};
};

} // namespace ignite
