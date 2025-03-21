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

package org.apache.ignite.internal.sql.engine.exec.rel;

import static org.apache.ignite.internal.util.CollectionUtils.nullOrEmpty;

import java.util.function.Supplier;
import org.apache.ignite.internal.sql.engine.exec.ExecutionContext;
import org.jetbrains.annotations.Nullable;

/** Offset, fetch|limit support node. */
public class LimitNode<RowT> extends AbstractNode<RowT> implements SingleNode<RowT>, Downstream<RowT> {
    /** Offset if its present, otherwise 0. */
    private final int offset;

    /** Fetch if its present, otherwise 0. */
    private final int fetch;

    /** Already processed (pushed to upstream) rows count. */
    private int rowsProcessed;

    /** Fetch can be unset, in this case we need all rows. */
    private @Nullable Supplier<Integer> fetchNode;

    /** Waiting results counter. */
    private int waiting;

    /**
     * Constructor.
     *
     * @param ctx     Execution context.
     */
    public LimitNode(
            ExecutionContext<RowT> ctx,
            Supplier<Integer> offsetNode,
            Supplier<Integer> fetchNode
    ) {
        super(ctx);

        offset = offsetNode == null ? 0 : offsetNode.get();
        fetch = fetchNode == null ? 0 : fetchNode.get();
        this.fetchNode = fetchNode;
    }

    /** {@inheritDoc} */
    @Override
    public void request(int rowsCnt) throws Exception {
        assert !nullOrEmpty(sources()) && sources().size() == 1;
        assert rowsCnt > 0;

        if (fetchNone()) {
            end();

            return;
        }

        if (offset > 0 && rowsProcessed == 0) {
            rowsCnt = offset + rowsCnt;
        }

        waiting = rowsCnt;

        if (fetch > 0) {
            rowsCnt = Math.min(rowsCnt, (fetch + offset) - rowsProcessed);
        }


        source().request(rowsCnt);
    }

    /** {@inheritDoc} */
    @Override
    public void push(RowT row) throws Exception {
        if (waiting == NOT_WAITING) {
            return;
        }
        ++rowsProcessed;

        --waiting;

        if (rowsProcessed > offset) {
            if (fetchNode == null || rowsProcessed <= fetch + offset) {
                downstream().push(row);
            }
        }

        if (fetch > 0 && rowsProcessed == fetch + offset && waiting > 0) {
            end();
        }
    }

    /** {@inheritDoc} */
    @Override
    public void end() throws Exception {
        if (waiting == NOT_WAITING) {
            return;
        }

        assert downstream() != null;

        waiting = NOT_WAITING;

        downstream().end();
    }

    /** {@inheritDoc} */
    @Override
    protected void rewindInternal() {
        rowsProcessed = 0;
    }

    /** {@inheritDoc} */
    @Override
    protected Downstream<RowT> requestDownstream(int idx) {
        if (idx != 0) {
            throw new IndexOutOfBoundsException();
        }

        return this;
    }

    /** {@code True} if requested 0 results, or all already processed. */
    private boolean fetchNone() {
        return (fetchNode != null && fetch == 0) || (fetch > 0 && rowsProcessed == fetch + offset);
    }
}
