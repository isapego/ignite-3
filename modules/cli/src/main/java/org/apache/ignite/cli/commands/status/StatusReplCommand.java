/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.apache.ignite.cli.commands.status;

import jakarta.inject.Inject;
import jakarta.inject.Singleton;
import org.apache.ignite.cli.call.status.StatusReplCall;
import org.apache.ignite.cli.commands.BaseCommand;
import org.apache.ignite.cli.commands.decorators.StatusReplDecorator;
import org.apache.ignite.cli.core.call.CallExecutionPipeline;
import org.apache.ignite.cli.core.call.EmptyCallInput;
import picocli.CommandLine.Command;
import picocli.CommandLine.Model.CommandSpec;
import picocli.CommandLine.Option;
import picocli.CommandLine.Spec;

/**
 * Command that prints status of ignite cluster.
 */
@Command(name = "status", description = "Prints status of the cluster.")
@Singleton
public class StatusReplCommand extends BaseCommand {

    /**
     * Cluster url option.
     */
    @SuppressWarnings("PMD.UnusedPrivateField")
    @Option(
            names = {"--cluster-url"}, description = "Url to cluster node."
    )
    private String clusterUrl;

    @Spec
    private CommandSpec commandSpec;

    @Inject
    private StatusReplCall statusReplCall;

    /** {@inheritDoc} */
    @Override
    public void run() {
        CallExecutionPipeline.builder(statusReplCall)
                .inputProvider(EmptyCallInput::new)
                .output(commandSpec.commandLine().getOut())
                .errOutput(commandSpec.commandLine().getErr())
                .decorator(new StatusReplDecorator())
                .build()
                .runPipeline();
    }
}
