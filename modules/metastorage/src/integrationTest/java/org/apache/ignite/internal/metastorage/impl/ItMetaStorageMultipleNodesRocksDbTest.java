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

package org.apache.ignite.internal.metastorage.impl;

import java.nio.file.Path;
import java.util.concurrent.ScheduledExecutorService;
import org.apache.ignite.internal.failure.NoOpFailureManager;
import org.apache.ignite.internal.metastorage.server.KeyValueStorage;
import org.apache.ignite.internal.metastorage.server.ReadOperationForCompactionTracker;
import org.apache.ignite.internal.metastorage.server.persistence.RocksDbKeyValueStorage;
import org.apache.ignite.internal.testframework.ExecutorServiceExtension;
import org.apache.ignite.internal.testframework.InjectExecutorService;
import org.junit.jupiter.api.extension.ExtendWith;

/** {@link ItMetaStorageMultipleNodesVsStorageTest} with {@link RocksDbKeyValueStorage} implementation. */
@ExtendWith(ExecutorServiceExtension.class)
public class ItMetaStorageMultipleNodesRocksDbTest extends ItMetaStorageMultipleNodesVsStorageTest {
    @InjectExecutorService
    private ScheduledExecutorService scheduledExecutorService;

    @Override
    public KeyValueStorage createStorage(String nodeName, Path path, ReadOperationForCompactionTracker readOperationForCompactionTracker) {
        return new RocksDbKeyValueStorage(
                nodeName,
                path.resolve("ms"),
                new NoOpFailureManager(),
                readOperationForCompactionTracker,
                scheduledExecutorService
        );
    }
}
