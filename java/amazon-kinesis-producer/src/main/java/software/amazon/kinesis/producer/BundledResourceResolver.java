/*
 * Copyright 2025 Amazon.com, Inc. or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package software.amazon.kinesis.producer;

import java.io.IOException;
import java.net.URL;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import lombok.AccessLevel;
import lombok.NoArgsConstructor;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Resolves classpath resources bundled with the KPL, preferring the copy that lives in the same artifact as a
 * given anchor class. The native binaries and CA certificates are stored at paths that are identical across every
 * KPL release, so a plain classpath search returns whichever KPL jar happens to come first. Matching on the
 * anchor's origin guarantees the Java classes and the resources they load come from the same release.
 */
@NoArgsConstructor(access = AccessLevel.PROTECTED)
final class BundledResourceResolver {

    private static final Logger log = LoggerFactory.getLogger(BundledResourceResolver.class);

    /**
     * Returns the URL prefix of the artifact that supplied {@code anchor}, or {@code null} if it cannot be
     * determined. For a jar this is {@code jar:file:/path/to.jar!/}; for an exploded directory it is the
     * directory URL.
     */
    static String originOf(Class<?> anchor) {
        String classResource = anchor.getName().replace('.', '/') + ".class";
        URL self = classLoaderOf(anchor).getResource(classResource);
        if (self == null) {
            return null;
        }
        String s = self.toString();
        return s.endsWith(classResource) ? s.substring(0, s.length() - classResource.length()) : null;
    }

    /**
     * Resolves {@code resource}, preferring the copy whose URL starts with {@code origin}. Falls back to the
     * first classpath match when no candidate shares the origin. Throws if the resource is absent entirely.
     */
    static URL resolve(Class<?> anchor, String origin, String resource) throws IOException {
        List<URL> candidates = Collections.list(classLoaderOf(anchor).getResources(resource));
        if (candidates.isEmpty()) {
            throw new IOException("Resource '" + resource + "' not found on the classpath. The KPL artifact that "
                    + "supplied " + anchor.getSimpleName() + " (" + origin + ") appears to be missing its bundled files.");
        }

        URL chosen = null;
        if (origin != null) {
            for (URL candidate : candidates) {
                if (candidate.toString().startsWith(origin)) {
                    chosen = candidate;
                    break;
                }
            }
        }

        if (chosen != null) {
            if (candidates.size() > 1) {
                log.debug("Found {} copies of '{}' on the classpath; using {}. Other copies: {}",
                        candidates.size(), resource, chosen, others(candidates, chosen));
            }
            return chosen;
        }

        // No match. Falling back to old behavior
        chosen = candidates.get(0);
        if (candidates.size() > 1) {
            log.warn("Found {} copies of '{}' on the classpath and none matched origin {}; falling back to {}. "
                    + "Other copies: {}", candidates.size(), resource, origin, chosen, others(candidates, chosen));
        } else {
            log.debug("Origin check inconclusive for '{}' (origin {}); using the only candidate {}",
                    resource, origin, chosen);
        }
        return chosen;
    }

    private static List<URL> others(List<URL> all, URL chosen) {
        List<URL> rest = new ArrayList<>(all);
        rest.remove(chosen);
        return rest;
    }

    private static ClassLoader classLoaderOf(Class<?> c) {
        ClassLoader cl = c.getClassLoader();
        return cl != null ? cl : ClassLoader.getSystemClassLoader();
    }

}
