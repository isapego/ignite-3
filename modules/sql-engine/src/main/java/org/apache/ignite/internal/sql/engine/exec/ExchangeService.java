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

package org.apache.ignite.internal.sql.engine.exec;

import java.util.List;
import java.util.UUID;
import org.apache.ignite.lang.IgniteInternalCheckedException;
import org.jetbrains.annotations.Nullable;

/**
 * ExchangeService interface.
 * TODO Documentation https://issues.apache.org/jira/browse/IGNITE-15859
 */
public interface ExchangeService extends LifecycleAware {
    /**
     * Sends a batch of data to remote node.
     *
     * @param nodeName Target node consistent ID.
     * @param qryId Query ID.
     * @param fragmentId Target fragment ID.
     * @param exchangeId Exchange ID.
     * @param batchId Batch ID.
     * @param last Last batch flag.
     * @param rows Data rows.
     */
    <RowT> void sendBatch(String nodeName, UUID qryId, long fragmentId, long exchangeId, int batchId, boolean last,
            List<RowT> rows) throws IgniteInternalCheckedException;

    /**
     * Requests batches from remote source.
     *
     * @param nodeName A consistent identifier of the node to request from.
     * @param queryId An identifier of the query.
     * @param fragmentId An identifier of the fragment to request from.
     * @param exchangeId An identifier of the exchange to request from.
     * @param amountOfBatches A count of batches to request.
     * @param state A state to propagate to the remote source.
     */
    void request(String nodeName, UUID queryId, long fragmentId, long exchangeId, int amountOfBatches,
            @Nullable SharedState state) throws IgniteInternalCheckedException;

    /**
     * Sends cancel request.
     *
     * @param nodeName Target node consistent ID.
     * @param qryId Query ID.
     * @param fragmentId Target fragment ID.
     * @param exchangeId Exchange ID.
     */
    void closeInbox(String nodeName, UUID qryId, long fragmentId, long exchangeId) throws IgniteInternalCheckedException;

    /**
     * Sends cancel request.
     *
     * @param nodeName Target node consistent ID.
     * @param qryId Query ID.
     */
    void closeQuery(String nodeName, UUID qryId) throws IgniteInternalCheckedException;

    /**
     * Send error.
     * TODO Documentation https://issues.apache.org/jira/browse/IGNITE-15859
     *
     * @param nodeName Target node consistent ID.
     * @param qryId Query ID.
     * @param fragmentId Source fragment ID.
     * @param err Exception to send.
     * @throws IgniteInternalCheckedException On error marshaling or send ErrorMessage.
     */
    void sendError(String nodeName, UUID qryId, long fragmentId, Throwable err) throws IgniteInternalCheckedException;

    /**
     * Alive.
     * TODO Documentation https://issues.apache.org/jira/browse/IGNITE-15859
     *
     * @param nodeName Target node consistent ID.
     * @return {@code true} if node is alive, {@code false} otherwise.
     */
    boolean alive(String nodeName);
}
