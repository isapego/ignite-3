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

package org.apache.ignite.internal.storage.lease;

import java.util.UUID;
import org.apache.ignite.internal.tostring.S;

/** Represents information about partition lease. */
public class LeaseInfo {
    private final long leaseStartTime;
    private final UUID primaryReplicaNodeId;
    private final String primaryReplicaNodeName;

    /** Constructor. */
    public LeaseInfo(long leaseStartTime, UUID primaryReplicaNodeId, String primaryReplicaNodeName) {
        this.leaseStartTime = leaseStartTime;
        this.primaryReplicaNodeId = primaryReplicaNodeId;
        this.primaryReplicaNodeName = primaryReplicaNodeName;
    }

    public long leaseStartTime() {
        return leaseStartTime;
    }

    public UUID primaryReplicaNodeId() {
        return primaryReplicaNodeId;
    }

    public String primaryReplicaNodeName() {
        return primaryReplicaNodeName;
    }

    @Override
    public boolean equals(Object o) {
        if (o == null || getClass() != o.getClass()) {
            return false;
        }

        LeaseInfo leaseInfo = (LeaseInfo) o;
        return leaseStartTime == leaseInfo.leaseStartTime && primaryReplicaNodeId.equals(leaseInfo.primaryReplicaNodeId)
                && primaryReplicaNodeName.equals(leaseInfo.primaryReplicaNodeName);
    }

    @Override
    public int hashCode() {
        int result = Long.hashCode(leaseStartTime);
        result = 31 * result + primaryReplicaNodeId.hashCode();
        result = 31 * result + primaryReplicaNodeName.hashCode();
        return result;
    }

    @Override
    public String toString() {
        return S.toString(this);
    }
}
