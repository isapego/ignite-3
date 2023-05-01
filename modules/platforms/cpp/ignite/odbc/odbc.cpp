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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#include "ignite/odbc/system/odbc_constants.h"
#include "ignite/odbc/system/system_dsn.h"
#include "log.h"
#include "utility.h"

#include "config/configuration.h"
#include "config/connection_string_parser.h"
#include "connection.h"
#include "dsn_config.h"
#include "environment.h"
#include "odbc.h"
#include "statement.h"
#include "type_traits.h"

/**
 * Handle window handle.
 * @param windowHandle Window handle.
 * @param config Configuration.
 * @return @c true on success and @c false otherwise.
 */
bool HandleParentWindow(SQLHWND windowHandle, ignite::config::Configuration &config)
{
#ifdef _WIN32
    if (windowHandle)
    {
        LOG_MSG("Parent window is passed. Creating configuration window.");
        return DisplayConnectionWindow(windowHandle, config);
    }
#else
    IGNITE_UNUSED(windowHandle);
    IGNITE_UNUSED(config);
#endif
    return true;
}

namespace ignite
{
    SQLRETURN SQLGetInfo(SQLHDBC        conn,
                         SQLUSMALLINT   infoType,
                         SQLPOINTER     infoValue,
                         SQLSMALLINT    infoValueMax,
                         SQLSMALLINT*   length)
    {
        using Connection;
        using config::ConnectionInfo;

        LOG_MSG("SQLGetInfo called: "
            << infoType << " (" << ConnectionInfo::InfoTypeToString(infoType) << "), "
            << std::hex << reinterpret_cast<size_t>(infoValue) << ", " << infoValueMax << ", "
            << std::hex << reinterpret_cast<size_t>(length));

        Connection *connection = reinterpret_cast<Connection*>(conn);

        if (!connection)
            return SQL_INVALID_HANDLE;

        connection->GetInfo(infoType, infoValue, infoValueMax, length);

        return connection->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLAllocHandle(SQLSMALLINT type, SQLHANDLE parent, SQLHANDLE* result)
    {
        //LOG_MSG("SQLAllocHandle called");
        switch (type)
        {
            case SQL_HANDLE_ENV:
                return SQLAllocEnv(result);

            case SQL_HANDLE_DBC:
                return SQLAllocConnect(parent, result);

            case SQL_HANDLE_STMT:
                return SQLAllocStmt(parent, result);

            case SQL_HANDLE_DESC:
            {
                using Connection;
                Connection *connection = reinterpret_cast<Connection*>(parent);

                if (!connection)
                    return SQL_INVALID_HANDLE;

                if (result)
                    *result = 0;

                connection->GetDiagnosticRecords().Reset();
                connection->AddStatusRecord(sql_state::SIM001_FUNCTION_NOT_SUPPORTED,
                                            "The HandleType argument was SQL_HANDLE_DESC, and "
                                            "the driver does not support allocating a descriptor handle");

                return SQL_ERROR;
            }
            default:
                break;
        }

        *result = 0;
        return SQL_ERROR;
    }

    SQLRETURN SQLAllocEnv(SQLHENV* env)
    {
        using Environment;

        LOG_MSG("SQLAllocEnv called");

        *env = reinterpret_cast<SQLHENV>(new Environment());

        return SQL_SUCCESS;
    }

    SQLRETURN SQLAllocConnect(SQLHENV env, SQLHDBC* conn)
    {
        using Environment;
        using Connection;

        LOG_MSG("SQLAllocConnect called");

        *conn = SQL_NULL_HDBC;

        Environment *environment = reinterpret_cast<Environment*>(env);

        if (!environment)
            return SQL_INVALID_HANDLE;

        Connection *connection = environment->CreateConnection();

        if (!connection)
            return environment->GetDiagnosticRecords().GetReturnCode();

        *conn = reinterpret_cast<SQLHDBC>(connection);

        return SQL_SUCCESS;
    }

    SQLRETURN SQLAllocStmt(SQLHDBC conn, SQLHSTMT* stmt)
    {
        using Connection;
        using Statement;

        LOG_MSG("SQLAllocStmt called");

        *stmt = SQL_NULL_HDBC;

        Connection *connection = reinterpret_cast<Connection*>(conn);

        if (!connection)
            return SQL_INVALID_HANDLE;

        Statement *statement = connection->CreateStatement();

        *stmt = reinterpret_cast<SQLHSTMT>(statement);

        return connection->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLFreeHandle(SQLSMALLINT type, SQLHANDLE handle)
    {
        switch (type)
        {
            case SQL_HANDLE_ENV:
                return SQLFreeEnv(handle);

            case SQL_HANDLE_DBC:
                return SQLFreeConnect(handle);

            case SQL_HANDLE_STMT:
                return SQLFreeStmt(handle, SQL_DROP);

            case SQL_HANDLE_DESC:
            default:
                break;
        }

        return SQL_ERROR;
    }

    SQLRETURN SQLFreeEnv(SQLHENV env)
    {
        using Environment;

        LOG_MSG("SQLFreeEnv called: " << env);

        Environment *environment = reinterpret_cast<Environment*>(env);

        if (!environment)
            return SQL_INVALID_HANDLE;

        delete environment;

        return SQL_SUCCESS;
    }

    SQLRETURN SQLFreeConnect(SQLHDBC conn)
    {
        using Connection;

        LOG_MSG("SQLFreeConnect called");

        Connection *connection = reinterpret_cast<Connection*>(conn);

        if (!connection)
            return SQL_INVALID_HANDLE;

        connection->Deregister();

        delete connection;

        return SQL_SUCCESS;
    }

    SQLRETURN SQLFreeStmt(SQLHSTMT stmt, SQLUSMALLINT option)
    {
        using Statement;

        LOG_MSG("SQLFreeStmt called [option=" << option << ']');

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        if (option == SQL_DROP)
        {
            delete statement;
            return SQL_SUCCESS;
        }

        statement->FreeResources(option);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLCloseCursor(SQLHSTMT stmt)
    {
        using Statement;

        LOG_MSG("SQLCloseCursor called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        statement->Close();

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLDriverConnect(SQLHDBC      conn,
                               SQLHWND      windowHandle,
                               SQLCHAR*     inConnectionString,
                               SQLSMALLINT  inConnectionStringLen,
                               SQLCHAR*     outConnectionString,
                               SQLSMALLINT  outConnectionStringBufferLen,
                               SQLSMALLINT* outConnectionStringLen,
                               SQLUSMALLINT driverCompletion)
    {
        IGNITE_UNUSED(driverCompletion);

        using Connection;
        using diagnostic_record_storage;
        using utility::SqlStringToString;
        using utility::CopyStringToBuffer;

        LOG_MSG("SQLDriverConnect called");
        if (inConnectionString)
            LOG_MSG("Connection String: [" << inConnectionString << "]");

        Connection *connection = reinterpret_cast<Connection*>(conn);

        if (!connection)
            return SQL_INVALID_HANDLE;

        std::string connectStr = SqlStringToString(inConnectionString, inConnectionStringLen);
        connection->Establish(connectStr, windowHandle);

        diagnostic_record_storage& diag = connection->GetDiagnosticRecords();
        if (!diag.IsSuccessful())
            return diag.GetReturnCode();

        size_t reslen = CopyStringToBuffer(connectStr,
            reinterpret_cast<char*>(outConnectionString),
            static_cast<size_t>(outConnectionStringBufferLen));

        if (outConnectionStringLen)
            *outConnectionStringLen = static_cast<SQLSMALLINT>(reslen);

        if (outConnectionString)
            LOG_MSG(outConnectionString);

        return diag.GetReturnCode();
    }

    SQLRETURN SQLConnect(SQLHDBC        conn,
                         SQLCHAR*       serverName,
                         SQLSMALLINT    serverNameLen,
                         SQLCHAR*       userName,
                         SQLSMALLINT    userNameLen,
                         SQLCHAR*       auth,
                         SQLSMALLINT    authLen)
    {
        IGNITE_UNUSED(userName);
        IGNITE_UNUSED(userNameLen);
        IGNITE_UNUSED(auth);
        IGNITE_UNUSED(authLen);

        using Connection;
        using config::Configuration;
        using utility::SqlStringToString;

        LOG_MSG("SQLConnect called\n");

        Connection *connection = reinterpret_cast<Connection*>(conn);

        if (!connection)
            return SQL_INVALID_HANDLE;

        config::Configuration config;

        std::string dsn = SqlStringToString(serverName, serverNameLen);

        LOG_MSG("DSN: " << dsn);

        ReadDsnConfiguration(dsn.c_str(), config, &connection->GetDiagnosticRecords());

        connection->Establish(config);

        return connection->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLDisconnect(SQLHDBC conn)
    {
        using Connection;

        LOG_MSG("SQLDisconnect called");

        Connection *connection = reinterpret_cast<Connection*>(conn);

        if (!connection)
            return SQL_INVALID_HANDLE;

        connection->Release();

        return connection->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLPrepare(SQLHSTMT stmt, SQLCHAR* query, SQLINTEGER queryLen)
    {
        using Statement;
        using utility::SqlStringToString;

        LOG_MSG("SQLPrepare called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        std::string sql = SqlStringToString(query, queryLen);

        LOG_MSG("SQL: " << sql);

        statement->PrepareSqlQuery(sql);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLExecute(SQLHSTMT stmt)
    {
        using Statement;

        LOG_MSG("SQLExecute called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        statement->ExecuteSqlQuery();

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLExecDirect(SQLHSTMT stmt, SQLCHAR* query, SQLINTEGER queryLen)
    {
        using Statement;
        using utility::SqlStringToString;

        LOG_MSG("SQLExecDirect called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        std::string sql = SqlStringToString(query, queryLen);

        LOG_MSG("SQL: " << sql);

        statement->ExecuteSqlQuery(sql);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLBindCol(SQLHSTMT       stmt,
                         SQLUSMALLINT   colNum,
                         SQLSMALLINT    targetType,
                         SQLPOINTER     targetValue,
                         SQLLEN         bufferLength,
                         SQLLEN*        strLengthOrIndicator)
    {
        using namespace type_traits;

        using Statement;
        using application_data_buffer;

        LOG_MSG("SQLBindCol called: index=" << colNum << ", type=" << targetType << 
                ", targetValue=" << reinterpret_cast<size_t>(targetValue) << 
                ", bufferLength=" << bufferLength << 
                ", lengthInd=" << reinterpret_cast<size_t>(strLengthOrIndicator));

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        statement->BindColumn(colNum, targetType, targetValue, bufferLength, strLengthOrIndicator);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLFetch(SQLHSTMT stmt)
    {
        using Statement;

        LOG_MSG("SQLFetch called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        statement->FetchRow();

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLFetchScroll(SQLHSTMT stmt, SQLSMALLINT orientation, SQLLEN offset)
    {
        using Statement;

        LOG_MSG("SQLFetchScroll called");
        LOG_MSG("Orientation: " << orientation << " Offset: " << offset);

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        statement->FetchScroll(orientation, offset);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLExtendedFetch(SQLHSTMT         stmt,
                               SQLUSMALLINT     orientation,
                               SQLLEN           offset,
                               SQLULEN*         rowCount,
                               SQLUSMALLINT*    rowStatusArray)
    {
        LOG_MSG("SQLExtendedFetch called");

        SQLRETURN res = SQLFetchScroll(stmt, orientation, offset);

        if (res == SQL_SUCCESS)
        {
            if (rowCount)
                *rowCount = 1;

            if (rowStatusArray)
                rowStatusArray[0] = SQL_ROW_SUCCESS;
        }
        else if (res == SQL_NO_DATA && rowCount)
            *rowCount = 0;

        return res;
    }

    SQLRETURN SQLNumResultCols(SQLHSTMT stmt, SQLSMALLINT *columnNum)
    {
        using Statement;
        using meta::ColumnMetaVector;

        LOG_MSG("SQLNumResultCols called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        int32_t res = statement->GetColumnNumber();

        if (columnNum)
        {
            *columnNum = static_cast<SQLSMALLINT>(res);
            LOG_MSG("columnNum: " << *columnNum);
        }

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLTables(SQLHSTMT    stmt,
                        SQLCHAR*    catalogName,
                        SQLSMALLINT catalogNameLen,
                        SQLCHAR*    schemaName,
                        SQLSMALLINT schemaNameLen,
                        SQLCHAR*    tableName,
                        SQLSMALLINT tableNameLen,
                        SQLCHAR*    tableType,
                        SQLSMALLINT tableTypeLen)
    {
        using Statement;
        using utility::SqlStringToString;

        LOG_MSG("SQLTables called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        std::string catalog = SqlStringToString(catalogName, catalogNameLen);
        std::string schema = SqlStringToString(schemaName, schemaNameLen);
        std::string table = SqlStringToString(tableName, tableNameLen);
        std::string tableTypeStr = SqlStringToString(tableType, tableTypeLen);

        LOG_MSG("catalog: " << catalog);
        LOG_MSG("schema: " << schema);
        LOG_MSG("table: " << table);
        LOG_MSG("tableType: " << tableTypeStr);

        statement->ExecuteGetTablesMetaQuery(catalog, schema, table, tableTypeStr);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLColumns(SQLHSTMT       stmt,
                         SQLCHAR*       catalogName,
                         SQLSMALLINT    catalogNameLen,
                         SQLCHAR*       schemaName,
                         SQLSMALLINT    schemaNameLen,
                         SQLCHAR*       tableName,
                         SQLSMALLINT    tableNameLen,
                         SQLCHAR*       columnName,
                         SQLSMALLINT    columnNameLen)
    {
        using Statement;
        using utility::SqlStringToString;

        LOG_MSG("SQLColumns called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        std::string catalog = SqlStringToString(catalogName, catalogNameLen);
        std::string schema = SqlStringToString(schemaName, schemaNameLen);
        std::string table = SqlStringToString(tableName, tableNameLen);
        std::string column = SqlStringToString(columnName, columnNameLen);

        LOG_MSG("catalog: " << catalog);
        LOG_MSG("schema: " << schema);
        LOG_MSG("table: " << table);
        LOG_MSG("column: " << column);

        statement->ExecuteGetColumnsMetaQuery(schema, table, column);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLMoreResults(SQLHSTMT stmt)
    {
        using Statement;

        LOG_MSG("SQLMoreResults called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        statement->MoreResults();

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLBindParameter(SQLHSTMT     stmt,
                               SQLUSMALLINT paramIdx,
                               SQLSMALLINT  ioType,
                               SQLSMALLINT  bufferType,
                               SQLSMALLINT  paramSqlType,
                               SQLULEN      columnSize,
                               SQLSMALLINT  decDigits,
                               SQLPOINTER   buffer,
                               SQLLEN       bufferLen,
                               SQLLEN*      resLen)
    {
        using Statement;

        LOG_MSG("SQLBindParameter called: " << paramIdx << ", " << bufferType << ", " << paramSqlType);

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        statement->bind_parameter(paramIdx, ioType, bufferType, paramSqlType, columnSize, decDigits, buffer, bufferLen, resLen);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLNativeSql(SQLHDBC      conn,
                           SQLCHAR*     inQuery,
                           SQLINTEGER   inQueryLen,
                           SQLCHAR*     outQueryBuffer,
                           SQLINTEGER   outQueryBufferLen,
                           SQLINTEGER*  outQueryLen)
    {
        IGNITE_UNUSED(conn);

        using namespace utility;

        LOG_MSG("SQLNativeSql called");

        std::string in = SqlStringToString(inQuery, inQueryLen);

        CopyStringToBuffer(in, reinterpret_cast<char*>(outQueryBuffer),
            static_cast<size_t>(outQueryBufferLen));

        if (outQueryLen)
            *outQueryLen = std::min(outQueryBufferLen, static_cast<SQLINTEGER>(in.size()));

        return SQL_SUCCESS;
    }

    SQLRETURN SQLColAttribute(SQLHSTMT        stmt,
                              SQLUSMALLINT    columnNum,
                              SQLUSMALLINT    fieldId,
                              SQLPOINTER      strAttr,
                              SQLSMALLINT     bufferLen,
                              SQLSMALLINT*    strAttrLen,
                              SQLLEN*         numericAttr)
    {
        using Statement;
        using meta::ColumnMetaVector;
        using meta::ColumnMeta;

        LOG_MSG("SQLColAttribute called: " << fieldId << " (" << ColumnMeta::AttrIdToString(fieldId) << ")");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        // This is a special case
        if (fieldId == SQL_DESC_COUNT)
        {
            SQLSMALLINT val = 0;

            SQLRETURN res = SQLNumResultCols(stmt, &val);

            if (numericAttr && res == SQL_SUCCESS)
                *numericAttr = val;

            return res;
        }

        statement->GetColumnAttribute(columnNum, fieldId, reinterpret_cast<char*>(strAttr),
            bufferLen, strAttrLen, numericAttr);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLDescribeCol(SQLHSTMT       stmt,
                             SQLUSMALLINT   columnNum,
                             SQLCHAR*       columnNameBuf,
                             SQLSMALLINT    columnNameBufLen,
                             SQLSMALLINT*   columnNameLen,
                             SQLSMALLINT*   dataType,
                             SQLULEN*       columnSize,
                             SQLSMALLINT*   decimalDigits,
                             SQLSMALLINT*   nullable)
    {
        using Statement;
        using SQLLEN;

        LOG_MSG("SQLDescribeCol called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        statement->GetColumnAttribute(columnNum, SQL_DESC_NAME,
            reinterpret_cast<char*>(columnNameBuf), columnNameBufLen, columnNameLen, 0);

        SQLLEN dataTypeRes;
        SQLLEN columnSizeRes;
        SQLLEN decimalDigitsRes;
        SQLLEN nullableRes;

        statement->GetColumnAttribute(columnNum, SQL_DESC_TYPE, 0, 0, 0, &dataTypeRes);
        statement->GetColumnAttribute(columnNum, SQL_DESC_PRECISION, 0, 0, 0, &columnSizeRes);
        statement->GetColumnAttribute(columnNum, SQL_DESC_SCALE, 0, 0, 0, &decimalDigitsRes);
        statement->GetColumnAttribute(columnNum, SQL_DESC_NULLABLE, 0, 0, 0, &nullableRes);

        LOG_MSG("columnNum: " << columnNum);
        LOG_MSG("dataTypeRes: " << dataTypeRes);
        LOG_MSG("columnSizeRes: " << columnSizeRes);
        LOG_MSG("decimalDigitsRes: " << decimalDigitsRes);
        LOG_MSG("nullableRes: " << nullableRes);
        LOG_MSG("columnNameBuf: " << (columnNameBuf ? reinterpret_cast<const char*>(columnNameBuf) : "<null>"));
        LOG_MSG("columnNameLen: " << (columnNameLen ? *columnNameLen : -1));

        if (dataType)
            *dataType = static_cast<SQLSMALLINT>(dataTypeRes);

        if (columnSize)
            *columnSize = static_cast<SQLULEN>(columnSizeRes);

        if (decimalDigits)
            *decimalDigits = static_cast<SQLSMALLINT>(decimalDigitsRes);

        if (nullable)
            *nullable = static_cast<SQLSMALLINT>(nullableRes);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }


    SQLRETURN SQLRowCount(SQLHSTMT stmt, SQLLEN* rowCnt)
    {
        using Statement;

        LOG_MSG("SQLRowCount called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        int64_t res = statement->AffectedRows();

        LOG_MSG("Row count: " << res);

        if (rowCnt)
            *rowCnt = static_cast<SQLLEN>(res);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLForeignKeys(SQLHSTMT       stmt,
                             SQLCHAR*       primaryCatalogName,
                             SQLSMALLINT    primaryCatalogNameLen,
                             SQLCHAR*       primarySchemaName,
                             SQLSMALLINT    primarySchemaNameLen,
                             SQLCHAR*       primaryTableName,
                             SQLSMALLINT    primaryTableNameLen,
                             SQLCHAR*       foreignCatalogName,
                             SQLSMALLINT    foreignCatalogNameLen,
                             SQLCHAR*       foreignSchemaName,
                             SQLSMALLINT    foreignSchemaNameLen,
                             SQLCHAR*       foreignTableName,
                             SQLSMALLINT    foreignTableNameLen)
    {
        using Statement;
        using utility::SqlStringToString;

        LOG_MSG("SQLForeignKeys called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        std::string primaryCatalog = SqlStringToString(primaryCatalogName, primaryCatalogNameLen);
        std::string primarySchema = SqlStringToString(primarySchemaName, primarySchemaNameLen);
        std::string primaryTable = SqlStringToString(primaryTableName, primaryTableNameLen);
        std::string foreignCatalog = SqlStringToString(foreignCatalogName, foreignCatalogNameLen);
        std::string foreignSchema = SqlStringToString(foreignSchemaName, foreignSchemaNameLen);
        std::string foreignTable = SqlStringToString(foreignTableName, foreignTableNameLen);

        LOG_MSG("primaryCatalog: " << primaryCatalog);
        LOG_MSG("primarySchema: " << primarySchema);
        LOG_MSG("primaryTable: " << primaryTable);
        LOG_MSG("foreignCatalog: " << foreignCatalog);
        LOG_MSG("foreignSchema: " << foreignSchema);
        LOG_MSG("foreignTable: " << foreignTable);

        statement->ExecuteGetForeignKeysQuery(primaryCatalog, primarySchema,
            primaryTable, foreignCatalog, foreignSchema, foreignTable);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLGetStmtAttr(SQLHSTMT       stmt,
                             SQLINTEGER     attr,
                             SQLPOINTER     valueBuf,
                             SQLINTEGER     valueBufLen,
                             SQLINTEGER*    valueResLen)
    {
        using Statement;

        LOG_MSG("SQLGetStmtAttr called");

#ifdef _DEBUG
        using statement_attr_id_to_string;

        LOG_MSG("Attr: " << statement_attr_id_to_string(attr) << " (" << attr << ")");
#endif //_DEBUG

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        statement->GetAttribute(attr, valueBuf, valueBufLen, valueResLen);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLSetStmtAttr(SQLHSTMT    stmt,
                             SQLINTEGER  attr,
                             SQLPOINTER  value,
                             SQLINTEGER  valueLen)
    {
        using Statement;

        LOG_MSG("SQLSetStmtAttr called: " << attr);

#ifdef _DEBUG
        using statement_attr_id_to_string;

        LOG_MSG("Attr: " << statement_attr_id_to_string(attr) << " (" << attr << ")");
#endif //_DEBUG

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        statement->SetAttribute(attr, value, valueLen);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLPrimaryKeys(SQLHSTMT       stmt,
                             SQLCHAR*       catalogName,
                             SQLSMALLINT    catalogNameLen,
                             SQLCHAR*       schemaName,
                             SQLSMALLINT    schemaNameLen,
                             SQLCHAR*       tableName,
                             SQLSMALLINT    tableNameLen)
    {
        using Statement;
        using utility::SqlStringToString;

        LOG_MSG("SQLPrimaryKeys called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        std::string catalog = SqlStringToString(catalogName, catalogNameLen);
        std::string schema = SqlStringToString(schemaName, schemaNameLen);
        std::string table = SqlStringToString(tableName, tableNameLen);

        LOG_MSG("catalog: " << catalog);
        LOG_MSG("schema: " << schema);
        LOG_MSG("table: " << table);

        statement->ExecuteGetPrimaryKeysQuery(catalog, schema, table);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLNumParams(SQLHSTMT stmt, SQLSMALLINT* paramCnt)
    {
        using Statement;

        LOG_MSG("SQLNumParams called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        if (paramCnt)
        {
            uint16_t paramNum = 0;
            statement->get_parameters_number(paramNum);

            *paramCnt = static_cast<SQLSMALLINT>(paramNum);
        }

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLGetDiagField(SQLSMALLINT   handleType,
                              SQLHANDLE     handle,
                              SQLSMALLINT   recNum,
                              SQLSMALLINT   diagId,
                              SQLPOINTER    buffer,
                              SQLSMALLINT   bufferLen,
                              SQLSMALLINT*  resLen)
    {
        using namespace odbc;
        using namespace diagnostic;
        using namespace type_traits;

        using application_data_buffer;

        LOG_MSG("SQLGetDiagField called: " << recNum);

        SQLLEN outResLen;
        application_data_buffer outBuffer(odbc_native_type::AI_DEFAULT, buffer, bufferLen, &outResLen);

        sql_result result;

        diagnostic_field field = diagnostic_field_to_internal(diagId);

        switch (handleType)
        {
            case SQL_HANDLE_ENV:
            case SQL_HANDLE_DBC:
            case SQL_HANDLE_STMT:
            {
                Diagnosable *diag = reinterpret_cast<Diagnosable*>(handle);

                result = diag->GetDiagnosticRecords().GetField(recNum, field, outBuffer);

                break;
            }

            default:
            {
                result = sql_result::AI_NO_DATA;
                break;
            }
        }

        if (resLen && result == sql_result::AI_SUCCESS)
            *resLen = static_cast<SQLSMALLINT>(outResLen);

        return sql_result_to_return_code(result);
    }

    SQLRETURN SQLGetDiagRec(SQLSMALLINT     handleType,
                            SQLHANDLE       handle,
                            SQLSMALLINT     recNum,
                            SQLCHAR*        sqlState,
                            SQLINTEGER*     nativeError,
                            SQLCHAR*        msgBuffer,
                            SQLSMALLINT     msgBufferLen,
                            SQLSMALLINT*    msgLen)
    {
        using namespace utility;
        using namespace odbc;
        using namespace diagnostic;
        using namespace type_traits;

        using application_data_buffer;

        LOG_MSG("SQLGetDiagRec called");

        const diagnostic_record_storage* records = 0;

        switch (handleType)
        {
            case SQL_HANDLE_ENV:
            case SQL_HANDLE_DBC:
            case SQL_HANDLE_STMT:
            {
                Diagnosable *diag = reinterpret_cast<Diagnosable*>(handle);

                if (!diag)
                    return SQL_INVALID_HANDLE;

                records = &diag->GetDiagnosticRecords();

                break;
            }

            default:
                return SQL_INVALID_HANDLE;
        }

        if (recNum < 1 || msgBufferLen < 0)
            return SQL_ERROR;

        if (!records || recNum > records->GetStatusRecordsNumber())
            return SQL_NO_DATA;

        const DiagnosticRecord& record = records->GetStatusRecord(recNum);

        if (sqlState)
            CopyStringToBuffer(record.Getsql_state(), reinterpret_cast<char*>(sqlState), 6);

        if (nativeError)
            *nativeError = 0;

        const std::string& errMsg = record.GetMessageText();

        if (!msgBuffer || msgBufferLen < static_cast<SQLSMALLINT>(errMsg.size() + 1))
        {
            if (!msgLen)
                return SQL_ERROR;

            CopyStringToBuffer(errMsg, reinterpret_cast<char*>(msgBuffer), static_cast<size_t>(msgBufferLen));

            *msgLen = static_cast<SQLSMALLINT>(errMsg.size());

            return SQL_SUCCESS_WITH_INFO;
        }

        CopyStringToBuffer(errMsg, reinterpret_cast<char*>(msgBuffer), static_cast<size_t>(msgBufferLen));

        if (msgLen)
            *msgLen = static_cast<SQLSMALLINT>(errMsg.size());

        return SQL_SUCCESS;
    }

    SQLRETURN SQLGetTypeInfo(SQLHSTMT stmt, SQLSMALLINT type)
    {
        using Statement;

        LOG_MSG("SQLGetTypeInfo called: [type=" << type << ']');

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        statement->ExecuteGetTypeInfoQuery(static_cast<int16_t>(type));

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLEndTran(SQLSMALLINT handleType, SQLHANDLE handle, SQLSMALLINT completionType)
    {
        using namespace odbc;

        LOG_MSG("SQLEndTran called");

        SQLRETURN result;

        switch (handleType)
        {
            case SQL_HANDLE_ENV:
            {
                Environment *env = reinterpret_cast<Environment*>(handle);

                if (!env)
                    return SQL_INVALID_HANDLE;

                if (completionType == SQL_COMMIT)
                    env->TransactionCommit();
                else
                    env->TransactionRollback();

                result = env->GetDiagnosticRecords().GetReturnCode();

                break;
            }

            case SQL_HANDLE_DBC:
            {
                Connection *conn = reinterpret_cast<Connection*>(handle);

                if (!conn)
                    return SQL_INVALID_HANDLE;

                if (completionType == SQL_COMMIT)
                    conn->TransactionCommit();
                else
                    conn->TransactionRollback();

                result = conn->GetDiagnosticRecords().GetReturnCode();

                break;
            }

            default:
            {
                result = SQL_INVALID_HANDLE;

                break;
            }
        }

        return result;
    }

    SQLRETURN SQLGetData(SQLHSTMT       stmt,
                         SQLUSMALLINT   colNum,
                         SQLSMALLINT    targetType,
                         SQLPOINTER     targetValue,
                         SQLLEN         bufferLength,
                         SQLLEN*        strLengthOrIndicator)
    {
        using namespace type_traits;

        using Statement;
        using application_data_buffer;

        LOG_MSG("SQLGetData called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        odbc_native_type driverType = to_driver_type(targetType);

        application_data_buffer dataBuffer(driverType, targetValue, bufferLength, strLengthOrIndicator);

        statement->GetColumnData(colNum, dataBuffer);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLSetEnvAttr(SQLHENV     env,
                            SQLINTEGER  attr,
                            SQLPOINTER  value,
                            SQLINTEGER  valueLen)
    {
        using Environment;

        LOG_MSG("SQLSetEnvAttr called");
        LOG_MSG("Attribute: " << attr << ", Value: " << (size_t)value);

        Environment *environment = reinterpret_cast<Environment*>(env);

        if (!environment)
            return SQL_INVALID_HANDLE;

        environment->SetAttribute(attr, value, valueLen);

        return environment->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLGetEnvAttr(SQLHENV     env,
                            SQLINTEGER  attr,
                            SQLPOINTER  valueBuf,
                            SQLINTEGER  valueBufLen,
                            SQLINTEGER* valueResLen)
    {
        using namespace odbc;
        using namespace type_traits;

        using application_data_buffer;

        LOG_MSG("SQLGetEnvAttr called");

        Environment *environment = reinterpret_cast<Environment*>(env);

        if (!environment)
            return SQL_INVALID_HANDLE;

        SQLLEN outResLen;
        application_data_buffer outBuffer(odbc_native_type::AI_SIGNED_LONG, valueBuf,
            static_cast<int32_t>(valueBufLen), &outResLen);

        environment->GetAttribute(attr, outBuffer);

        if (valueResLen)
            *valueResLen = static_cast<SQLSMALLINT>(outResLen);

        return environment->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLSpecialColumns(SQLHSTMT    stmt,
                                SQLSMALLINT idType,
                                SQLCHAR*    catalogName,
                                SQLSMALLINT catalogNameLen,
                                SQLCHAR*    schemaName,
                                SQLSMALLINT schemaNameLen,
                                SQLCHAR*    tableName,
                                SQLSMALLINT tableNameLen,
                                SQLSMALLINT scope,
                                SQLSMALLINT nullable)
    {
        using namespace odbc;

        using utility::SqlStringToString;

        LOG_MSG("SQLSpecialColumns called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        std::string catalog = SqlStringToString(catalogName, catalogNameLen);
        std::string schema = SqlStringToString(schemaName, schemaNameLen);
        std::string table = SqlStringToString(tableName, tableNameLen);

        LOG_MSG("catalog: " << catalog);
        LOG_MSG("schema: " << schema);
        LOG_MSG("table: " << table);

        statement->ExecuteSpecialColumnsQuery(idType, catalog, schema, table, scope, nullable);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLParamData(SQLHSTMT stmt, SQLPOINTER* value)
    {
        using namespace ignite::odbc;

        LOG_MSG("SQLParamData called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        statement->SelectParam(value);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLPutData(SQLHSTMT stmt, SQLPOINTER data, SQLLEN strLengthOrIndicator)
    {
        using namespace ignite::odbc;

        LOG_MSG("SQLPutData called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        statement->put_data(data, strLengthOrIndicator);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLDescribeParam(SQLHSTMT     stmt,
                               SQLUSMALLINT paramNum,
                               SQLSMALLINT* dataType,
                               SQLULEN*     paramSize,
                               SQLSMALLINT* decimalDigits,
                               SQLSMALLINT* nullable)
    {
        using namespace ignite::odbc;

        LOG_MSG("SQLDescribeParam called");

        Statement *statement = reinterpret_cast<Statement*>(stmt);

        if (!statement)
            return SQL_INVALID_HANDLE;

        statement->DescribeParam(paramNum, dataType, paramSize, decimalDigits, nullable);

        return statement->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQLError(SQLHENV      env,
                       SQLHDBC      conn,
                       SQLHSTMT     stmt,
                       SQLCHAR*     state,
                       SQLINTEGER*  error,
                       SQLCHAR*     msgBuf,
                       SQLSMALLINT  msgBufLen,
                       SQLSMALLINT* msgResLen)
    {
        using namespace ignite::utility;
        using namespace ignite::odbc;
        using namespace ignite::diagnostic;
        using namespace ignite::type_traits;

        using ignite::application_data_buffer;

        LOG_MSG("SQLError called");

        SQLHANDLE handle = 0;

        if (env != 0)
            handle = static_cast<SQLHANDLE>(env);
        else if (conn != 0)
            handle = static_cast<SQLHANDLE>(conn);
        else if (stmt != 0)
            handle = static_cast<SQLHANDLE>(stmt);
        else
            return SQL_INVALID_HANDLE;

        Diagnosable *diag = reinterpret_cast<Diagnosable*>(handle);

        diagnostic_record_storage& records = diag->GetDiagnosticRecords();

        int32_t recNum = records.GetLastNonRetrieved();

        if (recNum < 1 || recNum > records.GetStatusRecordsNumber())
            return SQL_NO_DATA;

        DiagnosticRecord& record = records.GetStatusRecord(recNum);

        record.MarkRetrieved();

        if (state)
            CopyStringToBuffer(record.Getsql_state(), reinterpret_cast<char*>(state), 6);

        if (error)
            *error = 0;

        SQLLEN outResLen;
        application_data_buffer outBuffer(odbc_native_type::AI_CHAR, msgBuf, msgBufLen, &outResLen);

        outBuffer.put_string(record.GetMessageText());

        if (msgResLen)
            *msgResLen = static_cast<SQLSMALLINT>(outResLen);

        return SQL_SUCCESS;
    }

    SQLRETURN SQL_API SQLGetConnectAttr(SQLHDBC    conn,
                                        SQLINTEGER attr,
                                        SQLPOINTER valueBuf,
                                        SQLINTEGER valueBufLen,
                                        SQLINTEGER* valueResLen)
    {
        using namespace odbc;
        using namespace type_traits;

        using application_data_buffer;

        LOG_MSG("SQLGetConnectAttr called");

        Connection *connection = reinterpret_cast<Connection*>(conn);

        if (!connection)
            return SQL_INVALID_HANDLE;

        connection->GetAttribute(attr, valueBuf, valueBufLen, valueResLen);

        return connection->GetDiagnosticRecords().GetReturnCode();
    }

    SQLRETURN SQL_API SQLSetConnectAttr(SQLHDBC    conn,
                                        SQLINTEGER attr,
                                        SQLPOINTER value,
                                        SQLINTEGER valueLen)
    {
        using Connection;

        LOG_MSG("SQLSetConnectAttr called(" << attr << ", " << value << ")");

        Connection *connection = reinterpret_cast<Connection*>(conn);

        if (!connection)
            return SQL_INVALID_HANDLE;

        connection->SetAttribute(attr, value, valueLen);

        return connection->GetDiagnosticRecords().GetReturnCode();
    }

} // namespace ignite;
