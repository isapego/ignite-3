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

#include <cstdlib>

#include "connection.h"
#include "environment.h"
#include "ignite/odbc/system/odbc_constants.h"

namespace ignite
{
    namespace odbc
    {
        Environment::Environment() :
            connections(),
            odbcVersion(SQL_OV_ODBC3),
            odbcNts(SQL_TRUE)
        {
            srand(GetRandSeed());
        }

        Environment::~Environment()
        {
            // No-op.
        }

        Connection* Environment::CreateConnection()
        {
            Connection* connection;

            IGNITE_ODBC_API_CALL(InternalCreateConnection(connection));

            return connection;
        }

        void Environment::DeregisterConnection(Connection* conn)
        {
            connections.erase(conn);
        }

        sql_result Environment::InternalCreateConnection(Connection*& connection)
        {
            connection = new Connection(this);

            if (!connection)
            {
                add_status_record(sql_state::SHY001_MEMORY_ALLOCATION, "Not enough memory.");

                return sql_result::AI_ERROR;
            }

            connections.insert(connection);

            return sql_result::AI_SUCCESS;
        }

        void Environment::TransactionCommit()
        {
            IGNITE_ODBC_API_CALL(InternalTransactionCommit());
        }

        sql_result Environment::InternalTransactionCommit()
        {
            sql_result res = sql_result::AI_SUCCESS;

            for (ConnectionSet::iterator it = connections.begin(); it != connections.end(); ++it)
            {
                Connection* conn = *it;

                conn->TransactionCommit();

                diagnostic_record_storage& diag = conn->GetDiagnosticRecords();

                if (diag.get_status_records_number() > 0)
                {
                    add_status_record(diag.get_status_record(1));

                    res = sql_result::AI_SUCCESS_WITH_INFO;
                }
            }

            return res;
        }

        void Environment::TransactionRollback()
        {
            IGNITE_ODBC_API_CALL(InternalTransactionRollback());
        }

        sql_result Environment::InternalTransactionRollback()
        {
            sql_result res = sql_result::AI_SUCCESS;

            for (ConnectionSet::iterator it = connections.begin(); it != connections.end(); ++it)
            {
                Connection* conn = *it;

                conn->TransactionRollback();

                diagnostic_record_storage& diag = conn->GetDiagnosticRecords();

                if (diag.get_status_records_number() > 0)
                {
                    add_status_record(diag.get_status_record(1));

                    res = sql_result::AI_SUCCESS_WITH_INFO;
                }
            }

            return res;
        }

        void Environment::SetAttribute(int32_t attr, void* value, int32_t len)
        {
            IGNITE_ODBC_API_CALL(InternalSetAttribute(attr, value, len));
        }

        sql_result Environment::InternalSetAttribute(int32_t attr, void* value, int32_t len)
        {
            IGNITE_UNUSED(len);

            environment_attribute attribute = environment_attribute_to_internal(attr);

            switch (attribute)
            {
                case environment_attribute::ODBC_VERSION:
                {
                    int32_t version = static_cast<int32_t>(reinterpret_cast<intptr_t>(value));

                    if (version != odbcVersion)
                    {
                        add_status_record(sql_state::S01S02_OPTION_VALUE_CHANGED,
                            "ODBC version is not supported.");

                        return sql_result::AI_SUCCESS_WITH_INFO;
                    }

                    return sql_result::AI_SUCCESS;
                }

                case environment_attribute::OUTPUT_NTS:
                {
                    int32_t nts = static_cast<int32_t>(reinterpret_cast<intptr_t>(value));

                    if (nts != odbcNts)
                    {
                        add_status_record(sql_state::S01S02_OPTION_VALUE_CHANGED,
                            "Only null-termination of strings is supported.");

                        return sql_result::AI_SUCCESS_WITH_INFO;
                    }

                    return sql_result::AI_SUCCESS;
                }

                case environment_attribute::UNKNOWN:
                default:
                    break;
            }

            add_status_record(sql_state::SHYC00_OPTIONAL_FEATURE_NOT_IMPLEMENTED,
                "Attribute is not supported.");

            return sql_result::AI_ERROR;
        }

        void Environment::GetAttribute(int32_t attr, application_data_buffer& buffer)
        {
            IGNITE_ODBC_API_CALL(InternalGetAttribute(attr, buffer));
        }

        sql_result Environment::InternalGetAttribute(int32_t attr, application_data_buffer& buffer)
        {
            environment_attribute attribute = environment_attribute_to_internal(attr);

            switch (attribute)
            {
                case environment_attribute::ODBC_VERSION:
                {
                    buffer.put_int32(odbcVersion);

                    return sql_result::AI_SUCCESS;
                }

                case environment_attribute::OUTPUT_NTS:
                {
                    buffer.put_int32(odbcNts);

                    return sql_result::AI_SUCCESS;
                }

                case environment_attribute::UNKNOWN:
                default:
                    break;
            }

            add_status_record(sql_state::SHYC00_OPTIONAL_FEATURE_NOT_IMPLEMENTED,
                "Attribute is not supported.");

            return sql_result::AI_ERROR;
        }
    }
}

