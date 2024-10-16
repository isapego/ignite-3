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

package org.apache.ignite.internal.rest;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.is;
import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertThrows;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import io.micronaut.http.HttpRequest;
import io.micronaut.http.client.HttpClient;
import io.micronaut.http.client.annotation.Client;
import io.micronaut.http.client.exceptions.HttpClientResponseException;
import jakarta.inject.Inject;
import java.util.stream.Stream;
import org.apache.ignite.internal.rest.api.Problem;
import org.apache.ignite.internal.testframework.IntegrationTestBase;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.TestInfo;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;

/** Tests that before cluster is initialized, only a subset of endpoints are available. */
public class ItNodeOnlyEndpointsFilterNotInitializedTest extends IntegrationTestBase {

    private ObjectMapper mapper = new ObjectMapper();

    @Inject
    @Client(NODE_URL + "/management/v1")
    HttpClient client;

    private static Stream<Arguments> disabledEndpoints() {
        return Stream.of(
                Arguments.of("deployment/units"),
                Arguments.of("configuration/cluster"),
                Arguments.of("cluster/topology/logical")
        );
    }

    private static Stream<Arguments> enabledEndpoints() {
        return Stream.of(
                Arguments.of("node/state"),
                Arguments.of("configuration/node"),
                Arguments.of("configuration/node/rest"),
                Arguments.of("cluster/topology/physical")
        );
    }

    @BeforeAll
    public void setup(TestInfo testInfo) {
        // Given non-initialized cluster.
        startNodes(testInfo);
    }

    @AfterAll
    public void cleanup(TestInfo testInfo) throws Exception {
        stopNodes(testInfo);
    }

    @ParameterizedTest
    @MethodSource("disabledEndpoints")
    void clusterEndpointsDisabledWhenNotInitialized(String path) throws JsonProcessingException {
        HttpClientResponseException ex = assertThrows(
                HttpClientResponseException.class,
                () -> client.toBlocking().retrieve(HttpRequest.GET(path), String.class)
        );

        assertThat(ex.getStatus().getCode(), is(409));
        Problem problem = readProblem(ex);
        assertThat(problem.status(), is(409));
        assertThat(problem.detail(), is("Cluster is not initialized. Call /management/v1/cluster/init in order to initialize cluster."));
    }

    private Problem readProblem(HttpClientResponseException ex) throws JsonProcessingException {
        return mapper.readValue(ex.getResponse().getBody(String.class).get(), Problem.class);
    }

    @ParameterizedTest
    @MethodSource("enabledEndpoints")
    void nodeConfigAndClusterInitAreEnabled(String path) {
        // But node config and cluster init endpoints are enabled
        assertDoesNotThrow(
                () -> client.toBlocking().retrieve(HttpRequest.GET(path))
        );
    }
}
