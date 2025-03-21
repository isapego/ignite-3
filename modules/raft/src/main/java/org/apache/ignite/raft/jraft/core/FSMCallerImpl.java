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
package org.apache.ignite.raft.jraft.core;

import static java.util.stream.Collectors.toList;

import com.lmax.disruptor.EventHandler;
import com.lmax.disruptor.EventTranslator;
import com.lmax.disruptor.RingBuffer;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicLong;
import org.apache.ignite.internal.logger.IgniteLogger;
import org.apache.ignite.internal.logger.Loggers;
import org.apache.ignite.raft.jraft.Closure;
import org.apache.ignite.raft.jraft.FSMCaller;
import org.apache.ignite.raft.jraft.RaftMessagesFactory;
import org.apache.ignite.raft.jraft.StateMachine;
import org.apache.ignite.raft.jraft.Status;
import org.apache.ignite.raft.jraft.closure.ClosureQueue;
import org.apache.ignite.raft.jraft.closure.LoadSnapshotClosure;
import org.apache.ignite.raft.jraft.closure.SaveSnapshotClosure;
import org.apache.ignite.raft.jraft.closure.TaskClosure;
import org.apache.ignite.raft.jraft.conf.Configuration;
import org.apache.ignite.raft.jraft.conf.ConfigurationEntry;
import org.apache.ignite.raft.jraft.disruptor.DisruptorEventType;
import org.apache.ignite.raft.jraft.disruptor.NodeIdAware;
import org.apache.ignite.raft.jraft.disruptor.StripedDisruptor;
import org.apache.ignite.raft.jraft.entity.EnumOutter;
import org.apache.ignite.raft.jraft.entity.EnumOutter.ErrorType;
import org.apache.ignite.raft.jraft.entity.LeaderChangeContext;
import org.apache.ignite.raft.jraft.entity.LogEntry;
import org.apache.ignite.raft.jraft.entity.LogId;
import org.apache.ignite.raft.jraft.entity.NodeId;
import org.apache.ignite.raft.jraft.entity.PeerId;
import org.apache.ignite.raft.jraft.entity.RaftOutter;
import org.apache.ignite.raft.jraft.entity.SnapshotMetaBuilder;
import org.apache.ignite.raft.jraft.error.RaftError;
import org.apache.ignite.raft.jraft.error.RaftException;
import org.apache.ignite.raft.jraft.option.FSMCallerOptions;
import org.apache.ignite.raft.jraft.storage.LogManager;
import org.apache.ignite.raft.jraft.storage.snapshot.SnapshotReader;
import org.apache.ignite.raft.jraft.storage.snapshot.SnapshotWriter;
import org.apache.ignite.raft.jraft.util.DisruptorMetricSet;
import org.apache.ignite.raft.jraft.util.OnlyForTest;
import org.apache.ignite.raft.jraft.util.Requires;
import org.apache.ignite.raft.jraft.util.Utils;

/**
 * The finite state machine caller implementation.
 */
public class FSMCallerImpl implements FSMCaller {

    private static final IgniteLogger LOG = Loggers.forClass(FSMCallerImpl.class);

    /**
     * Task type
     */
    public enum TaskType {
        IDLE, //
        COMMITTED, //
        SNAPSHOT_SAVE, //
        SNAPSHOT_LOAD, //
        LEADER_STOP, //
        LEADER_START, //
        START_FOLLOWING, //
        STOP_FOLLOWING, //
        SHUTDOWN, //
        FLUSH, //
        ERROR;

        private String metricName;

        public String metricName() {
            if (this.metricName == null) {
                this.metricName = "fsm-" + name().toLowerCase().replaceAll("_", "-");
            }
            return this.metricName;
        }
    }

    /**
     * Apply task for disruptor.
     */
    public static class ApplyTask extends NodeIdAware {
        public TaskType type;
        // union fields
        public long committedIndex;
        long term;
        Status status;
        LeaderChangeContext leaderChangeCtx;
        Closure done;
        public CountDownLatch shutdownLatch;

        @Override
        public void reset() {
            super.reset();

            this.type = null;
            this.committedIndex = 0;
            this.term = 0;
            this.status = null;
            this.leaderChangeCtx = null;
            this.done = null;
            this.shutdownLatch = null;
        }
    }

    private class ApplyTaskHandler implements EventHandler<ApplyTask> {
        // max committed index in current batch, reset to -1 every batch
        private long maxCommittedIndex = -1;

        @Override
        public void onEvent(final ApplyTask event, final long sequence, final boolean endOfBatch) throws Exception {
            this.maxCommittedIndex = runApplyTask(event, this.maxCommittedIndex, endOfBatch);
        }
    }

    /** Raft node id. */
    private NodeId nodeId;

    private LogManager logManager;
    private StateMachine fsm;
    private ClosureQueue closureQueue;
    private final AtomicLong lastAppliedIndex;
    private long lastAppliedTerm;
    private Closure afterShutdown;
    private NodeImpl node;
    private volatile TaskType currTask;
    private final AtomicLong applyingIndex;
    private volatile RaftException error;
    private StripedDisruptor<ApplyTask> disruptor;
    private RingBuffer<ApplyTask> taskQueue;
    private volatile CountDownLatch shutdownLatch;
    private NodeMetrics nodeMetrics;
    private final CopyOnWriteArrayList<LastAppliedLogIndexListener> lastAppliedLogIndexListeners = new CopyOnWriteArrayList<>();
    private RaftMessagesFactory msgFactory;

    private volatile boolean shuttingDown;

    public FSMCallerImpl() {
        super();
        this.currTask = TaskType.IDLE;
        this.lastAppliedIndex = new AtomicLong(0);
        this.applyingIndex = new AtomicLong(0);
    }

    @Override
    public boolean init(final FSMCallerOptions opts) {
        this.nodeId = opts.getNode().getNodeId();

        this.logManager = opts.getLogManager();
        this.fsm = opts.getFsm();
        this.closureQueue = opts.getClosureQueue();
        this.afterShutdown = opts.getAfterShutdown();
        this.node = opts.getNode();
        this.nodeMetrics = this.node.getNodeMetrics();
        this.lastAppliedIndex.set(opts.getBootstrapId().getIndex());
        notifyLastAppliedIndexUpdated(this.lastAppliedIndex.get());
        this.lastAppliedTerm = opts.getBootstrapId().getTerm();

        disruptor = opts.getfSMCallerExecutorDisruptor();

        taskQueue = disruptor.subscribe(this.nodeId, new ApplyTaskHandler());

        if (this.nodeMetrics.getMetricRegistry() != null) {
            this.nodeMetrics.getMetricRegistry().register("jraft-fsm-caller-disruptor",
                new DisruptorMetricSet(this.taskQueue));
        }
        this.error = new RaftException(ErrorType.ERROR_TYPE_NONE);
        this.msgFactory = opts.getRaftMessagesFactory();
        LOG.info("Starts FSMCaller successfully [nodeId={}].", nodeId);
        return true;
    }

    @Override
    public synchronized void shutdown() {
        if (this.shutdownLatch != null) {
            return;
        }
        LOG.info("Shutting down FSMCaller...");

        this.shuttingDown = true;

        if (this.taskQueue != null) {
            final CountDownLatch latch = new CountDownLatch(1);
            this.shutdownLatch = latch;

            Utils.runInThread(this.node.getOptions().getCommonExecutor(), () -> this.taskQueue.publishEvent((task, sequence) -> {
                task.reset();

                task.nodeId = this.nodeId;
                task.type = TaskType.SHUTDOWN;
                task.shutdownLatch = latch;
            }));
        }
    }

    @Override
    public void addLastAppliedLogIndexListener(final LastAppliedLogIndexListener listener) {
        this.lastAppliedLogIndexListeners.add(listener);
    }

    @Override
    public void removeLastAppliedLogIndexListener(final LastAppliedLogIndexListener listener) {
        this.lastAppliedLogIndexListeners.remove(listener);
    }

    private boolean enqueueTask(final EventTranslator<ApplyTask> tpl) {
        if (this.shutdownLatch != null) {
            // Shutting down
            LOG.warn("FSMCaller is stopped, can not apply new task.");
            return false;
        }

        this.taskQueue.publishEvent(tpl);
        return true;
    }

    @Override
    public boolean onCommitted(final long committedIndex) {
        return enqueueTask((task, sequence) -> {
            task.nodeId = this.nodeId;
            task.handler = null;
            task.evtType = DisruptorEventType.REGULAR;
            task.type = TaskType.COMMITTED;
            task.committedIndex = committedIndex;
        });
    }

    /**
     * Flush all events in disruptor.
     */
    @OnlyForTest
    void flush() throws InterruptedException {
        final CountDownLatch latch = new CountDownLatch(1);
        enqueueTask((task, sequence) -> {
            task.nodeId = this.nodeId;
            task.handler = null;
            task.evtType = DisruptorEventType.REGULAR;
            task.type = TaskType.FLUSH;
            task.shutdownLatch = latch;
        });
        latch.await();
    }

    @Override
    public boolean onSnapshotLoad(final LoadSnapshotClosure done) {
        return enqueueTask((task, sequence) -> {
            task.nodeId = this.nodeId;
            task.handler = null;
            task.evtType = DisruptorEventType.REGULAR;
            task.type = TaskType.SNAPSHOT_LOAD;
            task.done = done;
        });
    }

    @Override
    public boolean onSnapshotSave(final SaveSnapshotClosure done) {
        return enqueueTask((task, sequence) -> {
            task.nodeId = this.nodeId;
            task.handler = null;
            task.evtType = DisruptorEventType.REGULAR;
            task.type = TaskType.SNAPSHOT_SAVE;
            task.done = done;
        });
    }

    @Override
    public boolean onLeaderStop(final Status status) {
        return enqueueTask((task, sequence) -> {
            task.nodeId = this.nodeId;
            task.handler = null;
            task.evtType = DisruptorEventType.REGULAR;
            task.type = TaskType.LEADER_STOP;
            task.status = new Status(status);
        });
    }

    @Override
    public boolean onLeaderStart(final long term) {
        return enqueueTask((task, sequence) -> {
            task.nodeId = this.nodeId;
            task.handler = null;
            task.evtType = DisruptorEventType.REGULAR;
            task.type = TaskType.LEADER_START;
            task.term = term;
        });
    }

    @Override
    public boolean onStartFollowing(final LeaderChangeContext ctx) {
        return enqueueTask((task, sequence) -> {
            task.nodeId = this.nodeId;
            task.handler = null;
            task.evtType = DisruptorEventType.REGULAR;
            task.type = TaskType.START_FOLLOWING;
            task.leaderChangeCtx = new LeaderChangeContext(ctx.getLeaderId(), ctx.getTerm(), ctx.getStatus());
        });
    }

    @Override
    public boolean onStopFollowing(final LeaderChangeContext ctx) {
        return enqueueTask((task, sequence) -> {
            task.nodeId = this.nodeId;
            task.handler = null;
            task.evtType = DisruptorEventType.REGULAR;
            task.type = TaskType.STOP_FOLLOWING;
            task.leaderChangeCtx = new LeaderChangeContext(ctx.getLeaderId(), ctx.getTerm(), ctx.getStatus());
        });
    }

    /**
     * Closure runs with an error.
     */
    public class OnErrorClosure implements Closure {
        private RaftException error;

        public OnErrorClosure(final RaftException error) {
            super();
            this.error = error;
        }

        public RaftException getError() {
            return this.error;
        }

        public void setError(final RaftException error) {
            this.error = error;
        }

        @Override
        public void run(final Status st) {
        }
    }

    @Override
    public boolean onError(final RaftException error) {
        if (!this.error.getStatus().isOk()) {
            LOG.warn("FSMCaller already in error status, ignore new error", error);
            return false;
        }
        final OnErrorClosure c = new OnErrorClosure(error);
        return enqueueTask((task, sequence) -> {
            task.nodeId = this.nodeId;
            task.handler = null;
            task.evtType = DisruptorEventType.REGULAR;
            task.type = TaskType.ERROR;
            task.done = c;
        });
    }

    @Override
    public long getLastAppliedIndex() {
        return this.lastAppliedIndex.get();
    }

    @Override
    public synchronized void join() throws InterruptedException {
        if (this.shutdownLatch != null) {
            this.shutdownLatch.await();
            this.disruptor.unsubscribe(this.nodeId);
            if (this.afterShutdown != null) {
                this.afterShutdown.run(Status.OK());
                this.afterShutdown = null;
            }
            this.shutdownLatch = null;
        }
    }

    @SuppressWarnings("ConstantConditions")
    private long runApplyTask(final ApplyTask task, long maxCommittedIndex, final boolean endOfBatch) {
        CountDownLatch shutdown = null;
        if (task.type == TaskType.COMMITTED) {
            if (task.committedIndex > maxCommittedIndex) {
                maxCommittedIndex = task.committedIndex;
            }
            task.reset();
        }
        else {
            if (maxCommittedIndex >= 0) {
                this.currTask = TaskType.COMMITTED;
                doCommitted(maxCommittedIndex);
                maxCommittedIndex = -1L; // reset maxCommittedIndex
            }
            final long startMs = Utils.monotonicMs();
            try {
                switch (task.type) {
                    case COMMITTED:
                        Requires.requireTrue(false, "Impossible");
                        break;
                    case SNAPSHOT_SAVE:
                        this.currTask = TaskType.SNAPSHOT_SAVE;
                        if (passByStatus(task.done)) {
                            doSnapshotSave((SaveSnapshotClosure) task.done);
                        }
                        break;
                    case SNAPSHOT_LOAD:
                        this.currTask = TaskType.SNAPSHOT_LOAD;
                        if (passByStatus(task.done)) {
                            doSnapshotLoad((LoadSnapshotClosure) task.done);
                        }
                        break;
                    case LEADER_STOP:
                        this.currTask = TaskType.LEADER_STOP;
                        doLeaderStop(task.status);
                        break;
                    case LEADER_START:
                        this.currTask = TaskType.LEADER_START;
                        doLeaderStart(task.term);
                        break;
                    case START_FOLLOWING:
                        this.currTask = TaskType.START_FOLLOWING;
                        doStartFollowing(task.leaderChangeCtx);
                        break;
                    case STOP_FOLLOWING:
                        this.currTask = TaskType.STOP_FOLLOWING;
                        doStopFollowing(task.leaderChangeCtx);
                        break;
                    case ERROR:
                        this.currTask = TaskType.ERROR;
                        doOnError((OnErrorClosure) task.done);
                        break;
                    case IDLE:
                        Requires.requireTrue(false, "Can't reach here");
                        break;
                    case SHUTDOWN:
                        this.currTask = TaskType.SHUTDOWN;
                        shutdown = task.shutdownLatch;
                        doShutdown();
                        break;
                    case FLUSH:
                        this.currTask = TaskType.FLUSH;
                        shutdown = task.shutdownLatch;
                        break;
                }
            }
            finally {
                this.nodeMetrics.recordLatency(task.type.metricName(), Utils.monotonicMs() - startMs);
                task.reset();
            }
        }
        try {
            if (endOfBatch && maxCommittedIndex >= 0) {
                this.currTask = TaskType.COMMITTED;
                doCommitted(maxCommittedIndex);
                maxCommittedIndex = -1L; // reset maxCommittedIndex
            }
            this.currTask = TaskType.IDLE;
            return maxCommittedIndex;
        }
        finally {
            if (shutdown != null) {
                shutdown.countDown();
            }
        }
    }

    private void doShutdown() {
        if (this.node != null) {
            this.node = null;
        }
        if (this.fsm != null) {
            this.fsm.onShutdown();
        }
    }

    private void notifyLastAppliedIndexUpdated(final long lastAppliedIndex) {
        for (final LastAppliedLogIndexListener listener : this.lastAppliedLogIndexListeners) {
            listener.onApplied(lastAppliedIndex);
        }
    }

    private void doCommitted(final long committedIndex) {
        if (!this.error.getStatus().isOk()) {
            return;
        }
        final long lastAppliedIndex = this.lastAppliedIndex.get();
        // We can tolerate the disorder of committed_index
        if (lastAppliedIndex >= committedIndex) {
            return;
        }
        final long startMs = Utils.monotonicMs();
        try {
            final List<Closure> closures = new ArrayList<>();
            final List<TaskClosure> taskClosures = new ArrayList<>();
            final long firstClosureIndex = this.closureQueue.popClosureUntil(committedIndex, closures, taskClosures);

            // Calls TaskClosure#onCommitted if necessary
            onTaskCommitted(taskClosures);

            Requires.requireTrue(firstClosureIndex >= 0, "Invalid firstClosureIndex");
            final IteratorImpl iterImpl = new IteratorImpl(this.fsm, this.logManager, closures, firstClosureIndex,
                lastAppliedIndex, committedIndex, this.applyingIndex, this.node.getOptions());

            while (!shuttingDown && iterImpl.isGood()) {
                final LogEntry logEntry = iterImpl.entry();
                if (logEntry.getType() != EnumOutter.EntryType.ENTRY_TYPE_DATA) {
                    if (logEntry.getType() == EnumOutter.EntryType.ENTRY_TYPE_CONFIGURATION) {
                        LogId logId = logEntry.getId();
                        ConfigurationEntry configurationEntry = new ConfigurationEntry(
                                logId.copy(),
                                new Configuration(logEntry.getPeers(), logEntry.getLearners()),
                                new Configuration()
                        );
                        if (logEntry.getOldPeers() != null && !logEntry.getOldPeers().isEmpty()) {
                            configurationEntry.setOldConf(new Configuration(logEntry.getOldPeers(), logEntry.getOldLearners()));
                        }

                        this.fsm.onRawConfigurationCommitted(configurationEntry, logId.getIndex(), logId.getTerm());

                        if (logEntry.getOldPeers() != null && !logEntry.getOldPeers().isEmpty()) {
                            // Joint stage is not supposed to be noticeable by end users.
                            this.fsm.onConfigurationCommitted(new Configuration(iterImpl.entry().getPeers()));
                        }
                    }
                    if (iterImpl.done() != null) {
                        // For other entries, we have nothing to do besides flush the
                        // pending tasks and run this closure to notify the caller that the
                        // entries before this one were successfully committed and applied.
                        iterImpl.done().run(Status.OK());
                    }
                    iterImpl.next();
                    continue;
                }

                // Apply data task to user state machine
                doApplyTasks(iterImpl);
            }

            if (iterImpl.hasError()) {
                setError(iterImpl.getError());
                iterImpl.runTheRestClosureWithError();
            } else if (shuttingDown) {
                iterImpl.runTheRestClosureWithShutdownException();
            }
            final long lastIndex = iterImpl.getIndex() - 1;
            final long lastTerm = this.logManager.getTerm(lastIndex);
            final LogId lastAppliedId = new LogId(lastIndex, lastTerm);
            this.lastAppliedIndex.set(lastIndex);
            this.lastAppliedTerm = lastTerm;
            this.logManager.setAppliedId(lastAppliedId);
            notifyLastAppliedIndexUpdated(lastIndex);
        }
        finally {
            this.nodeMetrics.recordLatency("fsm-commit", Utils.monotonicMs() - startMs);
        }
    }

    private void onTaskCommitted(final List<TaskClosure> closures) {
        for (int i = 0, size = closures.size(); i < size; i++) {
            final TaskClosure done = closures.get(i);
            done.onCommitted();
        }
    }

    private void doApplyTasks(final IteratorImpl iterImpl) {
        final IteratorWrapper iter = new IteratorWrapper(iterImpl, () -> shuttingDown);
        final long startApplyMs = Utils.monotonicMs();
        final long startIndex = iter.getIndex();
        try {
            this.fsm.onApply(iter);
        }
        finally {
            this.nodeMetrics.recordLatency("fsm-apply-tasks", Utils.monotonicMs() - startApplyMs);
            this.nodeMetrics.recordSize("fsm-apply-tasks-count", iter.getIndex() - startIndex);
        }
        if (iter.hasNext()) {
            LOG.error("Iterator is still valid, did you return before iterator reached the end?");
        }
        // Try move to next in case that we pass the same log twice.
        // But if we are shutting down, current entry is not applied, so we should not advance the iterator to allow a ShutdownException
        // being sent to its client.
        if (!shuttingDown) {
            iter.next();
        }
    }

    private void doSnapshotSave(final SaveSnapshotClosure done) {
        Requires.requireNonNull(done, "SaveSnapshotClosure is null");
        final long lastAppliedIndex = this.lastAppliedIndex.get();
        final ConfigurationEntry confEntry = this.logManager.getConfiguration(lastAppliedIndex);
        if (confEntry == null || confEntry.isEmpty()) {
            LOG.error("Empty conf entry for lastAppliedIndex={}", lastAppliedIndex);
            Utils.runClosureInThread(this.node.getOptions().getCommonExecutor(), done, new Status(RaftError.EINVAL,
                "Empty conf entry for lastAppliedIndex=%s", lastAppliedIndex));
            return;
        }

        SnapshotMetaBuilder metaBuilder = msgFactory.snapshotMeta()
            .cfgIndex(confEntry.getId().getIndex())
            .cfgTerm(confEntry.getId().getTerm())
            .lastIncludedIndex(lastAppliedIndex)
            .lastIncludedTerm(this.lastAppliedTerm)
            .peersList(confEntry.getConf().getPeers().stream().map(Object::toString).collect(toList()))
            .learnersList(confEntry.getConf().getLearners().stream().map(Object::toString).collect(toList()));

        if (confEntry.getOldConf() != null) {
            metaBuilder
                .oldPeersList(confEntry.getOldConf().getPeers().stream().map(Object::toString).collect(toList()))
                .oldLearnersList(confEntry.getOldConf().getLearners().stream().map(Object::toString).collect(toList()));
        }

        final SnapshotWriter writer = done.start(metaBuilder.build());
        if (writer == null) {
            done.run(new Status(RaftError.EINVAL, "snapshot_storage create SnapshotWriter failed"));
            return;
        }
        this.fsm.onSnapshotSave(writer, done);
    }

    @Override
    public String toString() {
        final StringBuilder sb = new StringBuilder("StateMachine [");
        switch (this.currTask) {
            case IDLE:
                sb.append("Idle");
                break;
            case COMMITTED:
                sb.append("Applying logIndex=").append(this.applyingIndex);
                break;
            case SNAPSHOT_SAVE:
                sb.append("Saving snapshot");
                break;
            case SNAPSHOT_LOAD:
                sb.append("Loading snapshot");
                break;
            case ERROR:
                sb.append("Notifying error");
                break;
            case LEADER_STOP:
                sb.append("Notifying leader stop");
                break;
            case LEADER_START:
                sb.append("Notifying leader start");
                break;
            case START_FOLLOWING:
                sb.append("Notifying start following");
                break;
            case STOP_FOLLOWING:
                sb.append("Notifying stop following");
                break;
            case SHUTDOWN:
                sb.append("Shutting down");
                break;
            default:
                break;
        }
        return sb.append(']').toString();
    }

    private void doSnapshotLoad(final LoadSnapshotClosure done) {
        Requires.requireNonNull(done, "LoadSnapshotClosure is null");
        final SnapshotReader reader = done.start();
        if (reader == null) {
            done.run(new Status(RaftError.EINVAL, "open SnapshotReader failed"));
            return;
        }
        final RaftOutter.SnapshotMeta meta = reader.load();
        if (meta == null) {
            done.run(new Status(RaftError.EINVAL, "SnapshotReader load meta failed"));
            if (reader.getRaftError() == RaftError.EIO) {
                final RaftException err = new RaftException(ErrorType.ERROR_TYPE_SNAPSHOT, RaftError.EIO,
                    "Fail to load snapshot meta");
                setError(err);
            }
            return;
        }
        final LogId lastAppliedId = new LogId(this.lastAppliedIndex.get(), this.lastAppliedTerm);
        final LogId snapshotId = new LogId(meta.lastIncludedIndex(), meta.lastIncludedTerm());
        if (lastAppliedId.compareTo(snapshotId) > 0) {
            done.run(new Status(
                RaftError.ESTALE,
                "Loading a stale snapshot last_applied_index=%d last_applied_term=%d snapshot_index=%d snapshot_term=%d",
                lastAppliedId.getIndex(), lastAppliedId.getTerm(), snapshotId.getIndex(), snapshotId.getTerm()));
            return;
        }
        if (!this.fsm.onSnapshotLoad(reader)) {
            done.run(new Status(-1, "StateMachine onSnapshotLoad failed"));
            final RaftException e = new RaftException(ErrorType.ERROR_TYPE_STATE_MACHINE,
                RaftError.ESTATEMACHINE, "StateMachine onSnapshotLoad failed");
            setError(e);
            return;
        }

        // JRaft tests (FSMCallerTest) use metas where any of peersList() and learnersList() might be null,
        // so we have to protect from this. In production, these methods never return null.
        if (meta.peersList() != null && meta.learnersList() != null) {
            ConfigurationEntry configurationEntry = new ConfigurationEntry(
                    new LogId(meta.cfgIndex(), meta.cfgTerm()),
                    new Configuration(
                            meta.peersList().stream().map(PeerId::parsePeer).collect(toList()),
                            meta.learnersList().stream().map(PeerId::parsePeer).collect(toList())
                    ),
                    new Configuration()
            );
            if (meta.oldPeersList() != null && !meta.oldPeersList().isEmpty()) {
                configurationEntry.setOldConf(new Configuration(
                        meta.oldPeersList().stream().map(PeerId::parsePeer).collect(toList()),
                        meta.oldLearnersList().stream().map(PeerId::parsePeer).collect(toList())
                ));
            }

            this.fsm.onRawConfigurationCommitted(configurationEntry, snapshotId.getIndex(), snapshotId.getTerm());
        }

        if (meta.oldPeersList() == null) {
            // Joint stage is not supposed to be noticeable by end users.
            final Configuration conf = new Configuration();
            if (meta.peersList() != null) {
                for (String metaPeer : meta.peersList()) {
                    final PeerId peer = new PeerId();
                    Requires.requireTrue(peer.parse(metaPeer), "Parse peer failed");
                    conf.addPeer(peer);
                }
            }
            this.fsm.onConfigurationCommitted(conf);
        }
        this.lastAppliedIndex.set(meta.lastIncludedIndex());
        this.lastAppliedTerm = meta.lastIncludedTerm();
        done.run(Status.OK());
    }

    private void doOnError(final OnErrorClosure done) {
        setError(done.getError());
    }

    private void doLeaderStop(final Status status) {
        this.fsm.onLeaderStop(status);
    }

    private void doLeaderStart(final long term) {
        this.fsm.onLeaderStart(term);
    }

    private void doStartFollowing(final LeaderChangeContext ctx) {
        this.fsm.onStartFollowing(ctx);
    }

    private void doStopFollowing(final LeaderChangeContext ctx) {
        this.fsm.onStopFollowing(ctx);
    }

    private void setError(final RaftException e) {
        if (this.error.getType() != ErrorType.ERROR_TYPE_NONE) {
            // already report
            return;
        }
        this.error = e;
        if (this.fsm != null) {
            this.fsm.onError(e);
        }
        if (this.node != null) {
            this.node.onError(e);
        }
    }

    @OnlyForTest
    RaftException getError() {
        return this.error;
    }

    private boolean passByStatus(final Closure done) {
        final Status status = this.error.getStatus();
        if (!status.isOk()) {
            if (done != null) {
                done.run(new Status(RaftError.EINVAL, "FSMCaller is in bad status=`%s`", status));
                return false;
            }
        }
        return true;
    }

    @Override
    public void describe(final Printer out) {
        out.print("  ") //
            .println(toString());
    }
}
