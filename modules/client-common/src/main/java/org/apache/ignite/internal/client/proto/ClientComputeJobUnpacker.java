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

package org.apache.ignite.internal.client.proto;

import static org.apache.ignite.lang.ErrorGroups.Client.PROTOCOL_ERR;
import static org.apache.ignite.marshalling.Marshaller.tryUnmarshalOrCast;

import org.apache.ignite.internal.binarytuple.inlineschema.TupleWithSchemaMarshalling;
import org.apache.ignite.lang.IgniteException;
import org.apache.ignite.marshalling.Marshaller;
import org.apache.ignite.sql.ColumnType;
import org.jetbrains.annotations.Nullable;

/** Unpacks job arguments and results. */
public final class ClientComputeJobUnpacker {
    /**
     * Unpacks compute job argument. If the marshaller is provided, it will be used to unmarshal the argument. If the marshaller is not
     * provided and the argument is a native column type or a tuple, it will be unpacked accordingly.
     *
     * @param marshaller Marshaller.
     * @param unpacker Unpacker.
     * @return Unpacked argument.
     */
    public static @Nullable Object unpackJobArgument(@Nullable Marshaller<?, byte[]> marshaller, ClientMessageUnpacker unpacker) {
        return unpack(marshaller, unpacker);
    }

    /**
     * Unpacks compute job result. If the marshaller is provided, it will be used to unmarshal the result. If the marshaller is not provided
     * and the result is a native column type or a tuple, it will be unpacked accordingly.
     *
     * @param marshaller Marshaller.
     * @param unpacker Unpacker.
     * @return Unpacked result.
     */
    public static @Nullable Object unpackJobResult(@Nullable Marshaller<?, byte[]> marshaller, ClientMessageUnpacker unpacker) {
        return unpack(marshaller, unpacker);
    }

    /** Underlying byte array expected to be in the following format: | typeId | value |. */
    private static @Nullable Object unpack(@Nullable Marshaller<?, byte[]> marshaller, ClientMessageUnpacker unpacker) {
        int typeId = unpacker.unpackInt();
        var type = ComputeJobType.Type.fromId(typeId);

        switch (type) {
            case NATIVE:
                ColumnType columnType = ColumnType.getById(typeId);
                if (columnType != ColumnType.BYTE_ARRAY) {
                    return unpacker.unpackObjectFromBinaryTuple();
                }

                if (marshaller != null) {
                    return tryUnmarshalOrCast(marshaller, unpacker.unpackObjectFromBinaryTuple());
                }

                return unpacker.unpackObjectFromBinaryTuple();

            case MARSHALLED_TUPLE:
                return TupleWithSchemaMarshalling.unmarshal(unpacker.readBinary());

            case MARSHALLED_OBJECT:
                return tryUnmarshalOrCast(marshaller, unpacker.readBinary());

            default:
                throw new IgniteException(PROTOCOL_ERR, "Unsupported compute job type id: " + typeId);
        }
    }
}
