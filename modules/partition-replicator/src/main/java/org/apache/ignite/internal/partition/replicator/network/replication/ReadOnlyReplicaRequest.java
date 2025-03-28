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

package org.apache.ignite.internal.partition.replicator.network.replication;

import java.util.UUID;
import org.apache.ignite.internal.hlc.HybridTimestamp;
import org.apache.ignite.internal.replicator.message.ReplicaRequest;
import org.jetbrains.annotations.Nullable;

/**
 * Read only replica request.
 */
public interface ReadOnlyReplicaRequest extends ReplicaRequest {
    HybridTimestamp readTimestamp();

    /** ID of the transaction in which this request is made. */
    // TODO: remove @Nullable after IGNITE-24120 is sorted out.
    @Nullable
    UUID transactionId();

    /** Inconsistent ID of coordinator of transaction to which this operation belongs. */
    // TODO: remove @Nullable after IGNITE-24120 is sorted out.
    @Nullable
    UUID coordinatorId();
}
