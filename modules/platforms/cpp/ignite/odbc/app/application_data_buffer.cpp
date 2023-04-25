﻿/*
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

#include <ignite/odbc/log.h>
#include <ignite/odbc/utility.h>
#include <ignite/odbc/app/application_data_buffer.h>
#include <ignite/odbc/system/odbc_constants.h>

#include <ignite/common/bits.h>

#include <algorithm>
#include <string>
#include <sstream>

namespace
{
    // Just copy bytes currently.
    // Only works for the ASCII character set.
    ignite::odbc::app::ConversionResult::Type StringToWstring(const char* str, int64_t strLen, SQLWCHAR* wstr, int64_t wstrLen)
    {
        using namespace ignite::odbc;

        if (wstrLen <= 0)
            return app::ConversionResult::AI_VARLEN_DATA_TRUNCATED;

        int64_t toCopy = std::min(strLen, wstrLen - 1);

        if (toCopy <= 0)
        {
            wstr[0] = 0;

            return app::ConversionResult::AI_VARLEN_DATA_TRUNCATED;
        }

        for (int64_t i = 0; i < toCopy; ++i)
            wstr[i] = str[i];

        wstr[toCopy] = 0;

        if (toCopy < strLen)
            return app::ConversionResult::AI_VARLEN_DATA_TRUNCATED;

        return app::ConversionResult::AI_SUCCESS;
    }
}

namespace ignite
{
    namespace odbc
    {
        namespace app
        {
            ApplicationDataBuffer::ApplicationDataBuffer() :
                type(type_traits::OdbcNativeType::AI_UNSUPPORTED),
                buffer(nullptr),
                buflen(0),
                reslen(nullptr),
                byteOffset(0),
                elementOffset(0)
            {
                // No-op.
            }

            ApplicationDataBuffer::ApplicationDataBuffer(type_traits::OdbcNativeType::Type type,
                void* buffer, SQLLEN buflen, SQLLEN* reslen) :
                type(type),
                buffer(buffer),
                buflen(buflen),
                reslen(reslen),
                byteOffset(0),
                elementOffset(0)
            {
                // No-op.
            }

            template<typename T>
            ConversionResult::Type ApplicationDataBuffer::PutNum(T value)
            {
                using namespace type_traits;

                LOG_MSG("value: " << value);

                SQLLEN* resLenPtr = GetResLen();
                void* dataPtr = GetData();

                switch (type)
                {
                    case OdbcNativeType::AI_SIGNED_TINYINT:
                    {
                        return PutNumToNumBuffer<signed char>(value);
                    }

                    case OdbcNativeType::AI_BIT:
                    case OdbcNativeType::AI_UNSIGNED_TINYINT:
                    {
                        return PutNumToNumBuffer<unsigned char>(value);
                    }

                    case OdbcNativeType::AI_SIGNED_SHORT:
                    {
                        return PutNumToNumBuffer<SQLSMALLINT>(value);
                    }

                    case OdbcNativeType::AI_UNSIGNED_SHORT:
                    {
                        return PutNumToNumBuffer<SQLUSMALLINT>(value);
                    }

                    case OdbcNativeType::AI_SIGNED_LONG:
                    {
                        return PutNumToNumBuffer<SQLINTEGER>(value);
                    }

                    case OdbcNativeType::AI_UNSIGNED_LONG:
                    {
                        return PutNumToNumBuffer<SQLUINTEGER>(value);
                    }

                    case OdbcNativeType::AI_SIGNED_BIGINT:
                    {
                        return PutNumToNumBuffer<SQLBIGINT>(value);
                    }

                    case OdbcNativeType::AI_UNSIGNED_BIGINT:
                    {
                        return PutNumToNumBuffer<SQLUBIGINT>(value);
                    }

                    case OdbcNativeType::AI_FLOAT:
                    {
                        return PutNumToNumBuffer<SQLREAL>(value);
                    }

                    case OdbcNativeType::AI_DOUBLE:
                    {
                        return PutNumToNumBuffer<SQLDOUBLE>(value);
                    }

                    case OdbcNativeType::AI_CHAR:
                    {
                        return PutValToStrBuffer<char>(value);
                    }

                    case OdbcNativeType::AI_WCHAR:
                    {
                        return PutValToStrBuffer<wchar_t>(value);
                    }

                    case OdbcNativeType::AI_NUMERIC:
                    {
                        if (dataPtr)
                        {
                            SQL_NUMERIC_STRUCT* out =
                                reinterpret_cast<SQL_NUMERIC_STRUCT*>(dataPtr);

                            uint64_t uval = static_cast<uint64_t>(value < 0 ? -value : value);

                            out->precision = bits::DigitLength(uval);
                            out->scale = 0;
                            out->sign = value < 0 ? 0 : 1;

                            memset(out->val, 0, SQL_MAX_NUMERIC_LEN);

                            memcpy(out->val, &uval, std::min<int>(SQL_MAX_NUMERIC_LEN, sizeof(uval)));
                        }

                        if (resLenPtr)
                            *resLenPtr = static_cast<SQLLEN>(sizeof(SQL_NUMERIC_STRUCT));

                        return ConversionResult::AI_SUCCESS;
                    }

                    case OdbcNativeType::AI_BINARY:
                    case OdbcNativeType::AI_DEFAULT:
                    {
                        if (dataPtr)
                            memcpy(dataPtr, &value, std::min(sizeof(value), static_cast<size_t>(buflen)));

                        if (resLenPtr)
                            *resLenPtr = sizeof(value);

                        return static_cast<size_t>(buflen) < sizeof(value) ?
                            ConversionResult::AI_VARLEN_DATA_TRUNCATED : ConversionResult::AI_SUCCESS;
                    }

                    case OdbcNativeType::AI_TDATE:
                    {
                        return PutDate(ignite_date(static_cast<int64_t>(value)));
                    }

                    case OdbcNativeType::AI_TTIMESTAMP:
                    {
                        return PutTimestamp(ignite_timestamp(static_cast<int64_t>(value)));
                    }

                    case OdbcNativeType::AI_TTIME:
                    {
                        return PutTime(ignite_time(static_cast<int64_t>(value)));
                    }

                    default:
                        break;
                }

                return ConversionResult::AI_UNSUPPORTED_CONVERSION;
            }

            template<typename TBuf, typename TIn>
            ConversionResult::Type ApplicationDataBuffer::PutNumToNumBuffer(TIn value)
            {
                void* dataPtr = GetData();
                SQLLEN* resLenPtr = GetResLen();

                if (dataPtr)
                {
                    TBuf* out = reinterpret_cast<TBuf*>(dataPtr);
                    *out = static_cast<TBuf>(value);
                }

                if (resLenPtr)
                    *resLenPtr = static_cast<SQLLEN>(sizeof(TBuf));

                return ConversionResult::AI_SUCCESS;
            }

            template<typename CharT, typename Tin>
            ConversionResult::Type ApplicationDataBuffer::PutValToStrBuffer(const Tin& value)
            {
                typedef std::basic_stringstream<CharT> ConverterType;

                ConverterType converter;

                converter << value;

                int32_t written = 0;

                return PutStrToStrBuffer<CharT>(converter.str(), written);
            }

            template<typename CharT>
            ConversionResult::Type ApplicationDataBuffer::PutValToStrBuffer(const int8_t& value)
            {
                typedef std::basic_stringstream<CharT> ConverterType;

                ConverterType converter;

                converter << static_cast<int>(value);

                int32_t written = 0;

                return PutStrToStrBuffer<CharT>(converter.str(), written);
            }

            template<typename OutCharT, typename InCharT>
            ConversionResult::Type ApplicationDataBuffer::PutStrToStrBuffer(const std::basic_string<InCharT>& value,
                int32_t& written)
            {
                written = 0;

                SQLLEN charSize = static_cast<SQLLEN>(sizeof(OutCharT));

                SQLLEN* resLenPtr = GetResLen();
                void* dataPtr = GetData();

                if (resLenPtr)
                    *resLenPtr = static_cast<SQLLEN>(value.size());

                if (!dataPtr)
                    return ConversionResult::AI_SUCCESS;

                if (buflen < charSize)
                    return ConversionResult::AI_VARLEN_DATA_TRUNCATED;

                OutCharT* out = reinterpret_cast<OutCharT*>(dataPtr);

                SQLLEN outLen = (buflen / charSize) - 1;

                SQLLEN toCopy = std::min<SQLLEN>(outLen, value.size());

                for (SQLLEN i = 0; i < toCopy; ++i)
                    out[i] = value[i];

                out[toCopy] = 0;

                written = static_cast<int32_t>(toCopy);

                if (toCopy < static_cast<SQLLEN>(value.size()))
                    return ConversionResult::AI_VARLEN_DATA_TRUNCATED;

                return ConversionResult::AI_SUCCESS;
            }

            ConversionResult::Type ApplicationDataBuffer::PutRawDataToBuffer(void *data, size_t len, int32_t& written)
            {
                SQLLEN iLen = static_cast<SQLLEN>(len);

                SQLLEN* resLenPtr = GetResLen();
                void* dataPtr = GetData();

                if (resLenPtr)
                    *resLenPtr = iLen;

                SQLLEN toCopy = std::min(buflen, iLen);

                if (dataPtr != 0 && toCopy > 0)
                    memcpy(dataPtr, data, static_cast<size_t>(toCopy));

                written = static_cast<int32_t>(toCopy);

                return toCopy < iLen ? ConversionResult::AI_VARLEN_DATA_TRUNCATED : ConversionResult::AI_SUCCESS;
            }

            ConversionResult::Type ApplicationDataBuffer::PutInt8(int8_t value)
            {
                return PutNum(value);
            }

            ConversionResult::Type ApplicationDataBuffer::PutInt16(int16_t value)
            {
                return PutNum(value);
            }

            ConversionResult::Type ApplicationDataBuffer::PutInt32(int32_t value)
            {
                return PutNum(value);
            }

            ConversionResult::Type ApplicationDataBuffer::PutInt64(int64_t value)
            {
                return PutNum(value);
            }

            ConversionResult::Type ApplicationDataBuffer::PutFloat(float value)
            {
                return PutNum(value);
            }

            ConversionResult::Type ApplicationDataBuffer::PutDouble(double value)
            {
                return PutNum(value);
            }

            ConversionResult::Type ApplicationDataBuffer::PutString(const std::string & value)
            {
                int32_t written = 0;

                return PutString(value, written);
            }

            ConversionResult::Type ApplicationDataBuffer::PutString(const std::string& value, int32_t& written)
            {
                using namespace type_traits;

                LOG_MSG("value: " << value);

                switch (type)
                {
                    case OdbcNativeType::AI_SIGNED_TINYINT:
                    case OdbcNativeType::AI_BIT:
                    case OdbcNativeType::AI_UNSIGNED_TINYINT:
                    case OdbcNativeType::AI_SIGNED_SHORT:
                    case OdbcNativeType::AI_UNSIGNED_SHORT:
                    case OdbcNativeType::AI_SIGNED_LONG:
                    case OdbcNativeType::AI_UNSIGNED_LONG:
                    case OdbcNativeType::AI_SIGNED_BIGINT:
                    case OdbcNativeType::AI_UNSIGNED_BIGINT:
                    case OdbcNativeType::AI_NUMERIC:
                    {
                        std::stringstream converter;

                        converter << value;

                        int64_t numValue;

                        converter >> numValue;

                        written = static_cast<int32_t>(value.size());

                        return PutNum(numValue);
                    }

                    case OdbcNativeType::AI_FLOAT:
                    case OdbcNativeType::AI_DOUBLE:
                    {
                        std::stringstream converter;

                        converter << value;

                        double numValue;

                        converter >> numValue;

                        written = static_cast<int32_t>(value.size());

                        return PutNum(numValue);
                    }

                    case OdbcNativeType::AI_CHAR:
                    case OdbcNativeType::AI_BINARY:
                    case OdbcNativeType::AI_DEFAULT:
                    {
                        return PutStrToStrBuffer<char>(value, written);
                    }

                    case OdbcNativeType::AI_WCHAR:
                    {
                        return PutStrToStrBuffer<wchar_t>(value, written);
                    }

                    default:
                        break;
                }

                return ConversionResult::AI_UNSUPPORTED_CONVERSION;
            }

            ConversionResult::Type ApplicationDataBuffer::PutGuid(const Guid& value)
            {
                using namespace type_traits;

                LOG_MSG("value: " << value);

                SQLLEN* resLenPtr = GetResLen();

                switch (type)
                {
                    case OdbcNativeType::AI_CHAR:
                    case OdbcNativeType::AI_BINARY:
                    case OdbcNativeType::AI_DEFAULT:
                    {
                        return PutValToStrBuffer<char>(value);
                    }

                    case OdbcNativeType::AI_WCHAR:
                    {
                        return PutValToStrBuffer<wchar_t>(value);
                    }

                    case OdbcNativeType::AI_GUID:
                    {
                        SQLGUID* guid = reinterpret_cast<SQLGUID*>(GetData());

                        guid->Data1 = static_cast<uint32_t>(value.GetMostSignificantBits() >> 32);
                        guid->Data2 = static_cast<uint16_t>(value.GetMostSignificantBits() >> 16);
                        guid->Data3 = static_cast<uint16_t>(value.GetMostSignificantBits());

                        uint64_t lsb = value.GetLeastSignificantBits();
                        for (size_t i = 0; i < sizeof(guid->Data4); ++i)
                            guid->Data4[i] = (lsb >> (sizeof(guid->Data4) - i - 1) * 8) & 0xFF;

                        if (resLenPtr)
                            *resLenPtr = static_cast<SQLLEN>(sizeof(SQLGUID));

                        return ConversionResult::AI_SUCCESS;
                    }

                    default:
                        break;
                }

                return ConversionResult::AI_UNSUPPORTED_CONVERSION;
            }

            ConversionResult::Type ApplicationDataBuffer::PutBinaryData(void *data, size_t len, int32_t& written)
            {
                using namespace type_traits;

                switch (type)
                {
                    case OdbcNativeType::AI_BINARY:
                    case OdbcNativeType::AI_DEFAULT:
                    {
                        return PutRawDataToBuffer(data, len, written);
                    }

                    case OdbcNativeType::AI_CHAR:
                    {
                        std::stringstream converter;

                        uint8_t *dataBytes = reinterpret_cast<uint8_t*>(data);

                        for (size_t i = 0; i < len; ++i)
                        {
                            converter << std::hex
                                      << std::setfill('0')
                                      << std::setw(2)
                                      << static_cast<unsigned>(dataBytes[i]);
                        }

                        return PutStrToStrBuffer<char>(converter.str(), written);
                    }

                    case OdbcNativeType::AI_WCHAR:
                    {
                        std::wstringstream converter;

                        uint8_t *dataBytes = reinterpret_cast<uint8_t*>(data);

                        for (size_t i = 0; i < len; ++i)
                        {
                            converter << std::hex
                                      << std::setfill(L'0')
                                      << std::setw(2)
                                      << static_cast<unsigned>(dataBytes[i]);
                        }

                        return PutStrToStrBuffer<wchar_t>(converter.str(), written);
                    }

                    default:
                        break;
                }
                
                return ConversionResult::AI_UNSUPPORTED_CONVERSION;
            }

            ConversionResult::Type ApplicationDataBuffer::PutNull()
            {
                SQLLEN* resLenPtr = GetResLen();

                if (!resLenPtr)
                    return ConversionResult::AI_INDICATOR_NEEDED;

                *resLenPtr = SQL_NULL_DATA;

                return ConversionResult::AI_SUCCESS;
            }

            ConversionResult::Type ApplicationDataBuffer::PutDecimal(const big_decimal& value)
            {
                using namespace type_traits;

                SQLLEN* resLenPtr = GetResLen();

                switch (type)
                {
                    case OdbcNativeType::AI_SIGNED_TINYINT:
                    case OdbcNativeType::AI_BIT:
                    case OdbcNativeType::AI_UNSIGNED_TINYINT:
                    case OdbcNativeType::AI_SIGNED_SHORT:
                    case OdbcNativeType::AI_UNSIGNED_SHORT:
                    case OdbcNativeType::AI_SIGNED_LONG:
                    case OdbcNativeType::AI_UNSIGNED_LONG:
                    case OdbcNativeType::AI_SIGNED_BIGINT:
                    case OdbcNativeType::AI_UNSIGNED_BIGINT:
                    {
                        PutNum<int64_t>(value.ToInt64());

                        return ConversionResult::AI_FRACTIONAL_TRUNCATED;
                    }

                    case OdbcNativeType::AI_FLOAT:
                    case OdbcNativeType::AI_DOUBLE:
                    {
                        PutNum<double>(value.ToDouble());

                        return ConversionResult::AI_FRACTIONAL_TRUNCATED;
                    }

                    case OdbcNativeType::AI_CHAR:
                    case OdbcNativeType::AI_WCHAR:
                    {
                        std::stringstream converter;

                        converter << value;

                        int32_t dummy = 0;

                        return PutString(converter.str(), dummy);
                    }

                    case OdbcNativeType::AI_NUMERIC:
                    {
                        SQL_NUMERIC_STRUCT* numeric =
                            reinterpret_cast<SQL_NUMERIC_STRUCT*>(GetData());

                        big_decimal zeroScaled;
                        value.SetScale(0, zeroScaled);

                        FixedSizeArray<int8_t> bytesBuffer;

                        const BigInteger& unscaled = zeroScaled.GetUnscaledValue();

                        unscaled.MagnitudeToBytes(bytesBuffer);

                        for (int32_t i = 0; i < SQL_MAX_NUMERIC_LEN; ++i)
                        {
                            int32_t bufIdx = bytesBuffer.GetSize() - 1 - i;
                            if (bufIdx >= 0)
                                numeric->val[i] = bytesBuffer[bufIdx];
                            else
                                numeric->val[i] = 0;
                        }

                        numeric->scale = 0;
                        numeric->sign = unscaled.GetSign() < 0 ? 0 : 1;
                        numeric->precision = unscaled.GetPrecision();

                        if (resLenPtr)
                            *resLenPtr = static_cast<SQLLEN>(sizeof(SQL_NUMERIC_STRUCT));

                        if (bytesBuffer.GetSize() > SQL_MAX_NUMERIC_LEN)
                            return ConversionResult::AI_FRACTIONAL_TRUNCATED;

                        return ConversionResult::AI_SUCCESS;
                    }

                    case OdbcNativeType::AI_DEFAULT:
                    case OdbcNativeType::AI_BINARY:
                    default:
                        break;
                }

                return ConversionResult::AI_UNSUPPORTED_CONVERSION;
            }

            ConversionResult::Type ApplicationDataBuffer::PutDate(const ignite_date& value)
            {
                using namespace type_traits;

                tm tmTime;

                DateToCTm(value, tmTime);

                SQLLEN* resLenPtr = GetResLen();
                void* dataPtr = GetData();

                switch (type)
                {
                    case OdbcNativeType::AI_CHAR:
                    {
                        char* buffer = reinterpret_cast<char*>(dataPtr);
                        const size_t valLen = sizeof("HHHH-MM-DD") - 1;

                        if (resLenPtr)
                            *resLenPtr = valLen;

                        if (buffer)
                            strftime(buffer, GetSize(), "%Y-%m-%d", &tmTime);

                        if (static_cast<SQLLEN>(valLen) + 1 > GetSize())
                            return ConversionResult::AI_VARLEN_DATA_TRUNCATED;

                        return ConversionResult::AI_SUCCESS;
                    }

                    case OdbcNativeType::AI_WCHAR:
                    {
                        SQLWCHAR* buffer = reinterpret_cast<SQLWCHAR*>(dataPtr);
                        const size_t valLen = sizeof("HHHH-MM-DD") - 1;

                        if (resLenPtr)
                            *resLenPtr = valLen;

                        if (buffer)
                        {
                            std::string tmp(valLen + 1, 0);

                            strftime(&tmp[0], tmp.size(), "%Y-%m-%d", &tmTime);

                            StringToWstring(&tmp[0], tmp.size(), buffer, GetSize());
                        }

                        if (static_cast<SQLLEN>(valLen) + 1 > GetSize())
                            return ConversionResult::AI_VARLEN_DATA_TRUNCATED;

                        return ConversionResult::AI_SUCCESS;
                    }

                    case OdbcNativeType::AI_TDATE:
                    {
                        SQL_DATE_STRUCT* buffer = reinterpret_cast<SQL_DATE_STRUCT*>(dataPtr);

                        buffer->year = tmTime.tm_year + 1900;
                        buffer->month = tmTime.tm_mon + 1;
                        buffer->day = tmTime.tm_mday;

                        if (resLenPtr)
                            *resLenPtr = static_cast<SQLLEN>(sizeof(SQL_DATE_STRUCT));

                        return ConversionResult::AI_SUCCESS;
                    }

                    case OdbcNativeType::AI_TTIME:
                    {
                        SQL_TIME_STRUCT* buffer = reinterpret_cast<SQL_TIME_STRUCT*>(dataPtr);

                        buffer->hour = tmTime.tm_hour;
                        buffer->minute = tmTime.tm_min;
                        buffer->second = tmTime.tm_sec;

                        if (resLenPtr)
                            *resLenPtr = static_cast<SQLLEN>(sizeof(SQL_TIME_STRUCT));

                        return ConversionResult::AI_SUCCESS;
                    }

                    case OdbcNativeType::AI_TTIMESTAMP:
                    {
                        SQL_TIMESTAMP_STRUCT* buffer = reinterpret_cast<SQL_TIMESTAMP_STRUCT*>(dataPtr);

                        buffer->year = tmTime.tm_year + 1900;
                        buffer->month = tmTime.tm_mon + 1;
                        buffer->day = tmTime.tm_mday;
                        buffer->hour = tmTime.tm_hour;
                        buffer->minute = tmTime.tm_min;
                        buffer->second = tmTime.tm_sec;
                        buffer->fraction = 0;

                        if (resLenPtr)
                            *resLenPtr = static_cast<SQLLEN>(sizeof(SQL_TIMESTAMP_STRUCT));

                        return ConversionResult::AI_SUCCESS;
                    }

                    case OdbcNativeType::AI_BINARY:
                    case OdbcNativeType::AI_DEFAULT:
                    case OdbcNativeType::AI_SIGNED_TINYINT:
                    case OdbcNativeType::AI_BIT:
                    case OdbcNativeType::AI_UNSIGNED_TINYINT:
                    case OdbcNativeType::AI_SIGNED_SHORT:
                    case OdbcNativeType::AI_UNSIGNED_SHORT:
                    case OdbcNativeType::AI_SIGNED_LONG:
                    case OdbcNativeType::AI_UNSIGNED_LONG:
                    case OdbcNativeType::AI_SIGNED_BIGINT:
                    case OdbcNativeType::AI_UNSIGNED_BIGINT:
                    case OdbcNativeType::AI_FLOAT:
                    case OdbcNativeType::AI_DOUBLE:
                    case OdbcNativeType::AI_NUMERIC:
                    default:
                        break;
                }

                return ConversionResult::AI_UNSUPPORTED_CONVERSION;
            }

            ConversionResult::Type ApplicationDataBuffer::PutTimestamp(const ignite_timestamp& value)
            {
                using namespace type_traits;

                tm tmTime;

                TimestampToCTm(value, tmTime);

                SQLLEN* resLenPtr = GetResLen();
                void* dataPtr = GetData();

                switch (type)
                {
                    case OdbcNativeType::AI_CHAR:
                    {
                        const size_t valLen = sizeof("HHHH-MM-DD HH:MM:SS") - 1;

                        if (resLenPtr)
                            *resLenPtr = valLen;

                        char* buffer = reinterpret_cast<char*>(dataPtr);

                        if (buffer)
                            strftime(buffer, GetSize(), "%Y-%m-%d %H:%M:%S", &tmTime);

                        if (static_cast<SQLLEN>(valLen) + 1 > GetSize())
                            return ConversionResult::AI_VARLEN_DATA_TRUNCATED;

                        return ConversionResult::AI_SUCCESS;
                    }

                    case OdbcNativeType::AI_WCHAR:
                    {
                        const size_t valLen = sizeof("HHHH-MM-DD HH:MM:SS") - 1;

                        if (resLenPtr)
                            *resLenPtr = valLen;

                        SQLWCHAR* buffer = reinterpret_cast<SQLWCHAR*>(dataPtr);

                        if (buffer)
                        {
                            std::string tmp(GetSize(), 0);

                            strftime(&tmp[0], GetSize(), "%Y-%m-%d %H:%M:%S", &tmTime);

                            StringToWstring(&tmp[0], tmp.size(), buffer, GetSize());
                        }

                        if (static_cast<SQLLEN>(valLen) + 1 > GetSize())
                            return ConversionResult::AI_VARLEN_DATA_TRUNCATED;

                        return ConversionResult::AI_SUCCESS;
                    }

                    case OdbcNativeType::AI_TDATE:
                    {
                        SQL_DATE_STRUCT* buffer = reinterpret_cast<SQL_DATE_STRUCT*>(dataPtr);

                        buffer->year = tmTime.tm_year + 1900;
                        buffer->month = tmTime.tm_mon + 1;
                        buffer->day = tmTime.tm_mday;

                        if (resLenPtr)
                            *resLenPtr = static_cast<SQLLEN>(sizeof(SQL_DATE_STRUCT));

                        return ConversionResult::AI_FRACTIONAL_TRUNCATED;
                    }

                    case OdbcNativeType::AI_TTIME:
                    {
                        SQL_TIME_STRUCT* buffer = reinterpret_cast<SQL_TIME_STRUCT*>(dataPtr);

                        buffer->hour = tmTime.tm_hour;
                        buffer->minute = tmTime.tm_min;
                        buffer->second = tmTime.tm_sec;

                        if (resLenPtr)
                            *resLenPtr = static_cast<SQLLEN>(sizeof(SQL_TIME_STRUCT));

                        return ConversionResult::AI_FRACTIONAL_TRUNCATED;
                    }

                    case OdbcNativeType::AI_TTIMESTAMP:
                    {
                        SQL_TIMESTAMP_STRUCT* buffer = reinterpret_cast<SQL_TIMESTAMP_STRUCT*>(dataPtr);

                        buffer->year = tmTime.tm_year + 1900;
                        buffer->month = tmTime.tm_mon + 1;
                        buffer->day = tmTime.tm_mday;
                        buffer->hour = tmTime.tm_hour;
                        buffer->minute = tmTime.tm_min;
                        buffer->second = tmTime.tm_sec;
                        buffer->fraction = value.GetSecondFraction();

                        if (resLenPtr)
                            *resLenPtr = static_cast<SQLLEN>(sizeof(SQL_TIMESTAMP_STRUCT));

                        return ConversionResult::AI_SUCCESS;
                    }

                    case OdbcNativeType::AI_BINARY:
                    case OdbcNativeType::AI_DEFAULT:
                    case OdbcNativeType::AI_SIGNED_TINYINT:
                    case OdbcNativeType::AI_BIT:
                    case OdbcNativeType::AI_UNSIGNED_TINYINT:
                    case OdbcNativeType::AI_SIGNED_SHORT:
                    case OdbcNativeType::AI_UNSIGNED_SHORT:
                    case OdbcNativeType::AI_SIGNED_LONG:
                    case OdbcNativeType::AI_UNSIGNED_LONG:
                    case OdbcNativeType::AI_SIGNED_BIGINT:
                    case OdbcNativeType::AI_UNSIGNED_BIGINT:
                    case OdbcNativeType::AI_FLOAT:
                    case OdbcNativeType::AI_DOUBLE:
                    case OdbcNativeType::AI_NUMERIC:
                    default:
                        break;
                }

                return ConversionResult::AI_UNSUPPORTED_CONVERSION;
            }

            ConversionResult::Type ApplicationDataBuffer::PutTime(const ignite_time& value)
            {
                using namespace type_traits;

                tm tmTime;

                TimeToCTm(value, tmTime);

                SQLLEN* resLenPtr = GetResLen();
                void* dataPtr = GetData();

                switch (type)
                {
                    case OdbcNativeType::AI_CHAR:
                    {
                        const size_t valLen = sizeof("HH:MM:SS") - 1;

                        if (resLenPtr)
                            *resLenPtr = sizeof("HH:MM:SS") - 1;

                        char* buffer = reinterpret_cast<char*>(dataPtr);

                        if (buffer)
                            strftime(buffer, GetSize(), "%H:%M:%S", &tmTime);

                        if (static_cast<SQLLEN>(valLen) + 1 > GetSize())
                            return ConversionResult::AI_VARLEN_DATA_TRUNCATED;

                        return ConversionResult::AI_SUCCESS;
                    }

                    case OdbcNativeType::AI_WCHAR:
                    {
                        const size_t valLen = sizeof("HH:MM:SS") - 1;

                        if (resLenPtr)
                            *resLenPtr = sizeof("HH:MM:SS") - 1;

                        SQLWCHAR* buffer = reinterpret_cast<SQLWCHAR*>(dataPtr);

                        if (buffer)
                        {
                            std::string tmp(GetSize(), 0);

                            strftime(&tmp[0], GetSize(), "%H:%M:%S", &tmTime);

                            StringToWstring(&tmp[0], tmp.size(), buffer, GetSize());
                        }

                        if (static_cast<SQLLEN>(valLen) + 1 > GetSize())
                            return ConversionResult::AI_VARLEN_DATA_TRUNCATED;

                        return ConversionResult::AI_SUCCESS;
                    }

                    case OdbcNativeType::AI_TTIME:
                    {
                        SQL_TIME_STRUCT* buffer = reinterpret_cast<SQL_TIME_STRUCT*>(dataPtr);

                        buffer->hour = tmTime.tm_hour;
                        buffer->minute = tmTime.tm_min;
                        buffer->second = tmTime.tm_sec;

                        if (resLenPtr)
                            *resLenPtr = static_cast<SQLLEN>(sizeof(SQL_TIME_STRUCT));

                        return ConversionResult::AI_SUCCESS;
                    }

                    case OdbcNativeType::AI_TTIMESTAMP:
                    {
                        SQL_TIMESTAMP_STRUCT* buffer = reinterpret_cast<SQL_TIMESTAMP_STRUCT*>(dataPtr);

                        buffer->year = tmTime.tm_year + 1900;
                        buffer->month = tmTime.tm_mon + 1;
                        buffer->day = tmTime.tm_mday;
                        buffer->hour = tmTime.tm_hour;
                        buffer->minute = tmTime.tm_min;
                        buffer->second = tmTime.tm_sec;
                        buffer->fraction = 0;

                        if (resLenPtr)
                            *resLenPtr = static_cast<SQLLEN>(sizeof(SQL_TIMESTAMP_STRUCT));

                        return ConversionResult::AI_SUCCESS;
                    }

                    case OdbcNativeType::AI_BINARY:
                    case OdbcNativeType::AI_DEFAULT:
                    case OdbcNativeType::AI_SIGNED_TINYINT:
                    case OdbcNativeType::AI_BIT:
                    case OdbcNativeType::AI_UNSIGNED_TINYINT:
                    case OdbcNativeType::AI_SIGNED_SHORT:
                    case OdbcNativeType::AI_UNSIGNED_SHORT:
                    case OdbcNativeType::AI_SIGNED_LONG:
                    case OdbcNativeType::AI_UNSIGNED_LONG:
                    case OdbcNativeType::AI_SIGNED_BIGINT:
                    case OdbcNativeType::AI_UNSIGNED_BIGINT:
                    case OdbcNativeType::AI_FLOAT:
                    case OdbcNativeType::AI_DOUBLE:
                    case OdbcNativeType::AI_NUMERIC:
                    case OdbcNativeType::AI_TDATE:
                    default:
                        break;
                }

                return ConversionResult::AI_UNSUPPORTED_CONVERSION;
            }

            std::string ApplicationDataBuffer::GetString(size_t maxLen) const
            {
                using namespace type_traits;
                std::string res;

                switch (type)
                {
                    case OdbcNativeType::AI_CHAR:
                    {
                        size_t paramLen = GetInputSize();

                        if (!paramLen)
                            break;

                        res = utility::SqlStringToString(
                            reinterpret_cast<const unsigned char*>(GetData()), static_cast<int32_t>(paramLen));

                        if (res.size() > maxLen)
                            res.resize(maxLen);

                        break;
                    }

                    case OdbcNativeType::AI_SIGNED_TINYINT:
                    case OdbcNativeType::AI_SIGNED_SHORT:
                    case OdbcNativeType::AI_SIGNED_LONG:
                    case OdbcNativeType::AI_SIGNED_BIGINT:
                    {
                        std::stringstream converter;

                        converter << GetNum<int64_t>();

                        res = converter.str();

                        break;
                    }

                    case OdbcNativeType::AI_BIT:
                    case OdbcNativeType::AI_UNSIGNED_TINYINT:
                    case OdbcNativeType::AI_UNSIGNED_SHORT:
                    case OdbcNativeType::AI_UNSIGNED_LONG:
                    case OdbcNativeType::AI_UNSIGNED_BIGINT:
                    {
                        std::stringstream converter;

                        converter << GetNum<uint64_t>();

                        res = converter.str();

                        break;
                    }

                    case OdbcNativeType::AI_FLOAT:
                    {
                        std::stringstream converter;

                        converter << GetNum<float>();

                        res = converter.str();

                        break;
                    }

                    case OdbcNativeType::AI_NUMERIC:
                    case OdbcNativeType::AI_DOUBLE:
                    {
                        std::stringstream converter;

                        converter << GetNum<double>();

                        res = converter.str();

                        break;
                    }

                    default:
                        break;
                }

                return res;
            }

            int8_t ApplicationDataBuffer::GetInt8() const
            {
                return GetNum<int8_t>();
            }

            int16_t ApplicationDataBuffer::GetInt16() const
            {
                return GetNum<int16_t>();
            }

            int32_t ApplicationDataBuffer::GetInt32() const
            {
                return GetNum<int32_t>();
            }

            int64_t ApplicationDataBuffer::GetInt64() const
            {
                return GetNum<int64_t>();
            }

            float ApplicationDataBuffer::GetFloat() const
            {
                return GetNum<float>();
            }

            double ApplicationDataBuffer::GetDouble() const
            {
                return GetNum<double>();
            }

            Guid ApplicationDataBuffer::GetGuid() const
            {
                using namespace type_traits;

                Guid res;

                switch (type)
                {
                    case OdbcNativeType::AI_CHAR:
                    {
                        SQLLEN paramLen = GetInputSize();

                        if (!paramLen)
                            break;

                        std::string str = utility::SqlStringToString(
                            reinterpret_cast<const unsigned char*>(GetData()), static_cast<int32_t>(paramLen));

                        std::stringstream converter;

                        converter << str;

                        converter >> res;

                        break;
                    }

                    case OdbcNativeType::AI_GUID:
                    {
                        const SQLGUID* guid = reinterpret_cast<const SQLGUID*>(GetData());

                        uint64_t msb = static_cast<uint64_t>(guid->Data1) << 32 |
                                       static_cast<uint64_t>(guid->Data2) << 16 |
                                       static_cast<uint64_t>(guid->Data3);

                        uint64_t lsb = 0;

                        for (size_t i = 0; i < sizeof(guid->Data4); ++i)
                            lsb = guid->Data4[i] << (sizeof(guid->Data4) - i - 1) * 8;

                        res = Guid(msb, lsb);

                        break;
                    }

                    default:
                        break;
                }

                return res;
            }

            const void* ApplicationDataBuffer::GetData() const
            {
                return ApplyOffset(buffer, GetElementSize());
            }

            const SQLLEN* ApplicationDataBuffer::GetResLen() const
            {
                return ApplyOffset(reslen, sizeof(*reslen));
            }

            void* ApplicationDataBuffer::GetData()
            {
                return ApplyOffset(buffer, GetElementSize());
            }

            SQLLEN* ApplicationDataBuffer::GetResLen()
            {
                return ApplyOffset(reslen, sizeof(*reslen));
            }


            template<typename T, typename V>
            inline V LoadPrimitive(const void* data) {
                T res = T();
                std::memcpy(&res, data, sizeof(res));
                return static_cast<V>(res);
            }

            template<typename T>
            T ApplicationDataBuffer::GetNum() const
            {
                using namespace type_traits;

                T res = T();

                switch (type)
                {
                    case OdbcNativeType::AI_CHAR:
                    {
                        SQLLEN paramLen = GetInputSize();

                        if (!paramLen)
                            break;

                        std::string str = GetString(paramLen);

                        std::stringstream converter;

                        converter << str;

                        // Workaround for char types which are recognised as
                        // symbolyc types and not numeric types.
                        if (sizeof(T) == 1)
                        {
                            short tmp;

                            converter >> tmp;

                            res = static_cast<T>(tmp);
                        }
                        else
                            converter >> res;

                        break;
                    }

                    case OdbcNativeType::AI_SIGNED_TINYINT:
                    {
                        res = LoadPrimitive<int8_t, T>(GetData());
                        break;
                    }

                    case OdbcNativeType::AI_BIT:
                    case OdbcNativeType::AI_UNSIGNED_TINYINT:
                    {
                        res = LoadPrimitive<uint8_t, T>(GetData());
                        break;
                    }

                    case OdbcNativeType::AI_SIGNED_SHORT:
                    {
                        res = LoadPrimitive<int16_t, T>(GetData());
                        break;
                    }

                    case OdbcNativeType::AI_UNSIGNED_SHORT:
                    {
                        res = LoadPrimitive<uint16_t, T>(GetData());
                        break;
                    }

                    case OdbcNativeType::AI_SIGNED_LONG:
                    {
                        res = LoadPrimitive<int32_t, T>(GetData());
                        break;
                    }

                    case OdbcNativeType::AI_UNSIGNED_LONG:
                    {
                        res = LoadPrimitive<uint32_t, T>(GetData());
                        break;
                    }

                    case OdbcNativeType::AI_SIGNED_BIGINT:
                    {
                        res = LoadPrimitive<int64_t, T>(GetData());
                        break;
                    }

                    case OdbcNativeType::AI_UNSIGNED_BIGINT:
                    {
                        res = LoadPrimitive<uint64_t, T>(GetData());
                        break;
                    }

                    case OdbcNativeType::AI_FLOAT:
                    {
                        res = LoadPrimitive<float, T>(GetData());
                        break;
                    }

                    case OdbcNativeType::AI_DOUBLE:
                    {
                        res = LoadPrimitive<double, T>(GetData());
                        break;
                    }

                    case OdbcNativeType::AI_NUMERIC:
                    {
                        const SQL_NUMERIC_STRUCT* numeric =
                            reinterpret_cast<const SQL_NUMERIC_STRUCT*>(GetData());

                        big_decimal dec(reinterpret_cast<const int8_t*>(numeric->val),
                            SQL_MAX_NUMERIC_LEN, numeric->scale, numeric->sign ? 1 : -1, false);

                        res = static_cast<T>(dec.ToInt64());

                        break;
                    }

                    default:
                        break;
                }

                return res;
            }

            ignite_date ApplicationDataBuffer::GetDate() const
            {
                using namespace type_traits;

                tm tmTime;

                std::memset(&tmTime, 0, sizeof(tmTime));

                switch (type)
                {
                    case OdbcNativeType::AI_TDATE:
                    {
                        const SQL_DATE_STRUCT* buffer = reinterpret_cast<const SQL_DATE_STRUCT*>(GetData());

                        tmTime.tm_year = buffer->year - 1900;
                        tmTime.tm_mon = buffer->month - 1;
                        tmTime.tm_mday = buffer->day;

                        break;
                    }

                    case OdbcNativeType::AI_TTIME:
                    {
                        const SQL_TIME_STRUCT* buffer = reinterpret_cast<const SQL_TIME_STRUCT*>(GetData());

                        tmTime.tm_year = 70;
                        tmTime.tm_mday = 1;
                        tmTime.tm_hour = buffer->hour;
                        tmTime.tm_min = buffer->minute;
                        tmTime.tm_sec = buffer->second;

                        break;
                    }

                    case OdbcNativeType::AI_TTIMESTAMP:
                    {
                        const SQL_TIMESTAMP_STRUCT* buffer = reinterpret_cast<const SQL_TIMESTAMP_STRUCT*>(GetData());

                        tmTime.tm_year = buffer->year - 1900;
                        tmTime.tm_mon = buffer->month - 1;
                        tmTime.tm_mday = buffer->day;
                        tmTime.tm_hour = buffer->hour;
                        tmTime.tm_min = buffer->minute;
                        tmTime.tm_sec = buffer->second;

                        break;
                    }

                    case OdbcNativeType::AI_CHAR:
                    {
                        SQLLEN paramLen = GetInputSize();

                        if (!paramLen)
                            break;

                        std::string str = utility::SqlStringToString(
                            reinterpret_cast<const unsigned char*>(GetData()), static_cast<int32_t>(paramLen));

                        sscanf(str.c_str(), "%d-%d-%d %d:%d:%d", &tmTime.tm_year, &tmTime.tm_mon,
                            &tmTime.tm_mday, &tmTime.tm_hour, &tmTime.tm_min, &tmTime.tm_sec);

                        tmTime.tm_year = tmTime.tm_year - 1900;
                        tmTime.tm_mon = tmTime.tm_mon - 1;

                        break;
                    }

                    default:
                        break;
                }

                return CTmToDate(tmTime);
            }

            ignite_timestamp ApplicationDataBuffer::GetTimestamp() const
            {
                using namespace type_traits;

                tm tmTime;

                std::memset(&tmTime, 0, sizeof(tmTime));

                int32_t nanos = 0;

                switch (type)
                {
                    case OdbcNativeType::AI_TDATE:
                    {
                        const SQL_DATE_STRUCT* buffer = reinterpret_cast<const SQL_DATE_STRUCT*>(GetData());

                        tmTime.tm_year = buffer->year - 1900;
                        tmTime.tm_mon = buffer->month - 1;
                        tmTime.tm_mday = buffer->day;

                        break;
                    }

                    case OdbcNativeType::AI_TTIME:
                    {
                        const SQL_TIME_STRUCT* buffer = reinterpret_cast<const SQL_TIME_STRUCT*>(GetData());

                        tmTime.tm_year = 70;
                        tmTime.tm_mday = 1;
                        tmTime.tm_hour = buffer->hour;
                        tmTime.tm_min = buffer->minute;
                        tmTime.tm_sec = buffer->second;

                        break;
                    }

                    case OdbcNativeType::AI_TTIMESTAMP:
                    {
                        const SQL_TIMESTAMP_STRUCT* buffer = reinterpret_cast<const SQL_TIMESTAMP_STRUCT*>(GetData());

                        tmTime.tm_year = buffer->year - 1900;
                        tmTime.tm_mon = buffer->month - 1;
                        tmTime.tm_mday = buffer->day;
                        tmTime.tm_hour = buffer->hour;
                        tmTime.tm_min = buffer->minute;
                        tmTime.tm_sec = buffer->second;

                        nanos = buffer->fraction;

                        break;
                    }

                    case OdbcNativeType::AI_CHAR:
                    {
                        SQLLEN paramLen = GetInputSize();

                        if (!paramLen)
                            break;

                        std::string str = utility::SqlStringToString(
                            reinterpret_cast<const unsigned char*>(GetData()), static_cast<int32_t>(paramLen));

                        sscanf(str.c_str(), "%d-%d-%d %d:%d:%d", &tmTime.tm_year, &tmTime.tm_mon,
                            &tmTime.tm_mday, &tmTime.tm_hour, &tmTime.tm_min, &tmTime.tm_sec);

                        tmTime.tm_year = tmTime.tm_year - 1900;
                        tmTime.tm_mon = tmTime.tm_mon - 1;

                        break;
                    }

                    default:
                        break;
                }

                return CTmToTimestamp(tmTime, nanos);
            }

            ignite_time ApplicationDataBuffer::GetTime() const
            {
                using namespace type_traits;

                tm tmTime;

                std::memset(&tmTime, 0, sizeof(tmTime));

                tmTime.tm_year = 70;
                tmTime.tm_mon = 0;
                tmTime.tm_mday = 1;

                switch (type)
                {
                    case OdbcNativeType::AI_TTIME:
                    {
                        const SQL_TIME_STRUCT* buffer = reinterpret_cast<const SQL_TIME_STRUCT*>(GetData());

                        tmTime.tm_hour = buffer->hour;
                        tmTime.tm_min = buffer->minute;
                        tmTime.tm_sec = buffer->second;

                        break;
                    }

                    case OdbcNativeType::AI_TTIMESTAMP:
                    {
                        const SQL_TIMESTAMP_STRUCT* buffer = reinterpret_cast<const SQL_TIMESTAMP_STRUCT*>(GetData());

                        tmTime.tm_hour = buffer->hour;
                        tmTime.tm_min = buffer->minute;
                        tmTime.tm_sec = buffer->second;

                        break;
                    }

                    case OdbcNativeType::AI_CHAR:
                    {
                        SQLLEN paramLen = GetInputSize();

                        if (!paramLen)
                            break;

                        std::string str = utility::SqlStringToString(
                            reinterpret_cast<const unsigned char*>(GetData()), static_cast<int32_t>(paramLen));

                        sscanf(str.c_str(), "%d:%d:%d", &tmTime.tm_hour, &tmTime.tm_min, &tmTime.tm_sec);

                        break;
                    }

                    default:
                        break;
                }

                return CTmToTime(tmTime);
            }

            void ApplicationDataBuffer::GetDecimal(big_decimal& val) const
            {
                using namespace type_traits;

                switch (type)
                {
                    case OdbcNativeType::AI_CHAR:
                    {
                        SQLLEN paramLen = GetInputSize();

                        if (!paramLen)
                            break;

                        std::string str = GetString(paramLen);

                        std::stringstream converter;

                        converter << str;

                        converter >> val;

                        break;
                    }

                    case OdbcNativeType::AI_SIGNED_TINYINT:
                    case OdbcNativeType::AI_BIT:
                    case OdbcNativeType::AI_SIGNED_SHORT:
                    case OdbcNativeType::AI_SIGNED_LONG:
                    case OdbcNativeType::AI_SIGNED_BIGINT:
                    {
                        val.AssignInt64(GetNum<int64_t>());

                        break;
                    }

                    case OdbcNativeType::AI_UNSIGNED_TINYINT:
                    case OdbcNativeType::AI_UNSIGNED_SHORT:
                    case OdbcNativeType::AI_UNSIGNED_LONG:
                    case OdbcNativeType::AI_UNSIGNED_BIGINT:
                    {
                        val.AssignUint64(GetNum<uint64_t>());

                        break;
                    }

                    case OdbcNativeType::AI_FLOAT:
                    case OdbcNativeType::AI_DOUBLE:
                    {
                        val.AssignDouble(GetNum<double>());

                        break;
                    }

                    case OdbcNativeType::AI_NUMERIC:
                    {
                        const SQL_NUMERIC_STRUCT* numeric =
                            reinterpret_cast<const SQL_NUMERIC_STRUCT*>(GetData());

                        big_decimal dec(reinterpret_cast<const int8_t*>(numeric->val),
                            SQL_MAX_NUMERIC_LEN, numeric->scale, numeric->sign ? 1 : -1, false);

                        val.Swap(dec);

                        break;
                    }

                    default:
                    {
                        val.AssignInt64(0);

                        break;
                    }
                }
            }

            template<typename T>
            T* ApplicationDataBuffer::ApplyOffset(T* ptr, size_t elemSize) const
            {
                if (!ptr)
                    return ptr;

                return utility::GetPointerWithOffset(ptr, byteOffset + elemSize * elementOffset);
            }

            bool ApplicationDataBuffer::IsDataAtExec() const
            {
                const SQLLEN* resLenPtr = GetResLen();

                if (!resLenPtr)
                    return false;

                int32_t ilen = static_cast<int32_t>(*resLenPtr);

                return ilen <= SQL_LEN_DATA_AT_EXEC_OFFSET || ilen == SQL_DATA_AT_EXEC;
            }

            SQLLEN ApplicationDataBuffer::GetDataAtExecSize() const
            {
                using namespace type_traits;

                switch (type)
                {
                    case OdbcNativeType::AI_WCHAR:
                    case OdbcNativeType::AI_CHAR:
                    case OdbcNativeType::AI_BINARY:
                    {
                        const SQLLEN* resLenPtr = GetResLen();

                        if (!resLenPtr)
                            return 0;

                        int32_t ilen = static_cast<int32_t>(*resLenPtr);

                        if (ilen <= SQL_LEN_DATA_AT_EXEC_OFFSET)
                            ilen = SQL_LEN_DATA_AT_EXEC(ilen);
                        else
                            ilen = 0;

                        if (type == OdbcNativeType::AI_WCHAR)
                            ilen *= 2;

                        return ilen;
                    }

                    case OdbcNativeType::AI_SIGNED_SHORT:
                    case OdbcNativeType::AI_UNSIGNED_SHORT:
                        return static_cast<SQLLEN>(sizeof(short));

                    case OdbcNativeType::AI_SIGNED_LONG:
                    case OdbcNativeType::AI_UNSIGNED_LONG:
                        return static_cast<SQLLEN>(sizeof(long));

                    case OdbcNativeType::AI_FLOAT:
                        return static_cast<SQLLEN>(sizeof(float));

                    case OdbcNativeType::AI_DOUBLE:
                        return static_cast<SQLLEN>(sizeof(double));

                    case OdbcNativeType::AI_BIT:
                    case OdbcNativeType::AI_SIGNED_TINYINT:
                    case OdbcNativeType::AI_UNSIGNED_TINYINT:
                        return static_cast<SQLLEN>(sizeof(char));

                    case OdbcNativeType::AI_SIGNED_BIGINT:
                    case OdbcNativeType::AI_UNSIGNED_BIGINT:
                        return static_cast<SQLLEN>(sizeof(SQLBIGINT));

                    case OdbcNativeType::AI_TDATE:
                        return static_cast<SQLLEN>(sizeof(SQL_DATE_STRUCT));

                    case OdbcNativeType::AI_TTIME:
                        return static_cast<SQLLEN>(sizeof(SQL_TIME_STRUCT));

                    case OdbcNativeType::AI_TTIMESTAMP:
                        return static_cast<SQLLEN>(sizeof(SQL_TIMESTAMP_STRUCT));

                    case OdbcNativeType::AI_NUMERIC:
                        return static_cast<SQLLEN>(sizeof(SQL_NUMERIC_STRUCT));

                    case OdbcNativeType::AI_GUID:
                        return static_cast<SQLLEN>(sizeof(SQLGUID));

                    case OdbcNativeType::AI_DEFAULT:
                    case OdbcNativeType::AI_UNSUPPORTED:
                    default:
                        break;
                }

                return 0;
            }

            SQLLEN ApplicationDataBuffer::GetElementSize() const
            {
                using namespace type_traits;

                switch (type)
                {
                    case OdbcNativeType::AI_WCHAR:
                    case OdbcNativeType::AI_CHAR:
                    case OdbcNativeType::AI_BINARY:
                        return buflen;

                    case OdbcNativeType::AI_SIGNED_SHORT:
                        return static_cast<SQLLEN>(sizeof(SQLSMALLINT));

                    case OdbcNativeType::AI_UNSIGNED_SHORT:
                        return static_cast<SQLLEN>(sizeof(SQLUSMALLINT));

                    case OdbcNativeType::AI_SIGNED_LONG:
                        return static_cast<SQLLEN>(sizeof(SQLUINTEGER));

                    case OdbcNativeType::AI_UNSIGNED_LONG:
                        return static_cast<SQLLEN>(sizeof(SQLINTEGER));

                    case OdbcNativeType::AI_FLOAT:
                        return static_cast<SQLLEN>(sizeof(SQLREAL));

                    case OdbcNativeType::AI_DOUBLE:
                        return static_cast<SQLLEN>(sizeof(SQLDOUBLE));

                    case OdbcNativeType::AI_SIGNED_TINYINT:
                        return static_cast<SQLLEN>(sizeof(SQLSCHAR));

                    case OdbcNativeType::AI_BIT:
                    case OdbcNativeType::AI_UNSIGNED_TINYINT:
                        return static_cast<SQLLEN>(sizeof(SQLCHAR));

                    case OdbcNativeType::AI_SIGNED_BIGINT:
                        return static_cast<SQLLEN>(sizeof(SQLBIGINT));

                    case OdbcNativeType::AI_UNSIGNED_BIGINT:
                        return static_cast<SQLLEN>(sizeof(SQLUBIGINT));

                    case OdbcNativeType::AI_TDATE:
                        return static_cast<SQLLEN>(sizeof(SQL_DATE_STRUCT));

                    case OdbcNativeType::AI_TTIME:
                        return static_cast<SQLLEN>(sizeof(SQL_TIME_STRUCT));

                    case OdbcNativeType::AI_TTIMESTAMP:
                        return static_cast<SQLLEN>(sizeof(SQL_TIMESTAMP_STRUCT));

                    case OdbcNativeType::AI_NUMERIC:
                        return static_cast<SQLLEN>(sizeof(SQL_NUMERIC_STRUCT));

                    case OdbcNativeType::AI_GUID:
                        return static_cast<SQLLEN>(sizeof(SQLGUID));

                    case OdbcNativeType::AI_DEFAULT:
                    case OdbcNativeType::AI_UNSUPPORTED:
                    default:
                        break;
                }

                return 0;
            }

            SQLLEN ApplicationDataBuffer::GetInputSize() const
            {
                if (!IsDataAtExec())
                {
                    const SQLLEN *len = GetResLen();

                    return len ? *len : SQL_NTS;
                }

                return GetDataAtExecSize();
            }
        }
    }
}

