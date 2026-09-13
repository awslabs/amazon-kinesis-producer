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

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.jar.JarEntry;
import java.util.jar.JarOutputStream;

import org.apache.commons.io.IOUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

/**
 * Builds throwaway jars on a child-less URLClassLoader so classpath order is fully controlled, then checks that
 * the resolver picks the copy of a resource that shares an origin with the anchor class rather than the first
 * one on the classpath.
 */
public class BundledResourceResolverTest {

    private static final String BINARY = "amazon-kinesis-producer-native-binaries/linux-x86_64/kinesis_producer";
    private static final String ANCHOR_CLASS = "software/amazon/kinesis/producer/KinesisProducer.class";

    private File tempDir;

    @Before
    public void before() throws IOException {
        tempDir = Files.createTempDirectory("kpl-resolver-test").toFile();
    }

    @After
    public void after() throws IOException {
        if (tempDir != null) {
            for (File f : tempDir.listFiles()) {
                f.delete();
            }
            tempDir.delete();
        }
    }

    @Test
    public void picksBinaryFromAnchorJarWhenStaleJarIsFirst() throws Exception {
        File stale = jar("stale.jar", BINARY, "OLD-BINARY");
        File current = jar("current.jar", BINARY, "NEW-BINARY", ANCHOR_CLASS, "class-bytes");

        try (URLClassLoader cl = loader(stale, current)) {
            Class<?> anchor = anchorIn(cl);
            String origin = BundledResourceResolver.originOf(anchor);
            assertNotNull(origin);
            assertTrue(origin.contains("current.jar"));

            URL chosen = BundledResourceResolver.resolve(anchor, origin, BINARY);
            assertEquals("NEW-BINARY", read(chosen));
        }
    }

    @Test
    public void picksBinaryFromAnchorJarWhenAnchorJarIsFirst() throws Exception {
        File current = jar("current.jar", BINARY, "NEW-BINARY", ANCHOR_CLASS, "class-bytes");
        File stale = jar("stale.jar", BINARY, "OLD-BINARY");

        try (URLClassLoader cl = loader(current, stale)) {
            Class<?> anchor = anchorIn(cl);
            URL chosen = BundledResourceResolver.resolve(anchor, BundledResourceResolver.originOf(anchor), BINARY);
            assertEquals("NEW-BINARY", read(chosen));
        }
    }

    @Test
    public void singleJarResolvesWithoutWarning() throws Exception {
        File current = jar("current.jar", BINARY, "NEW-BINARY", ANCHOR_CLASS, "class-bytes");

        try (URLClassLoader cl = loader(current)) {
            Class<?> anchor = anchorIn(cl);
            URL chosen = BundledResourceResolver.resolve(anchor, BundledResourceResolver.originOf(anchor), BINARY);
            assertEquals("NEW-BINARY", read(chosen));
        }
    }

    @Test
    public void fallsBackToFirstCandidateWhenOriginUnknown() throws Exception {
        File first = jar("first.jar", BINARY, "FIRST");
        File second = jar("second.jar", BINARY, "SECOND");

        try (URLClassLoader cl = loader(first, second)) {
            Class<?> anchor = anchorIn(cl);
            URL chosen = BundledResourceResolver.resolve(anchor, null, BINARY);
            assertEquals("FIRST", read(chosen));
        }
    }

    @Test
    public void throwsWhenResourceAbsent() throws Exception {
        File empty = jar("empty.jar", "unrelated.txt", "x");

        try (URLClassLoader cl = loader(empty)) {
            Class<?> anchor = loadDummyAnchor(cl);
            try {
                BundledResourceResolver.resolve(anchor, "jar:file:/nowhere.jar!/", BINARY);
                fail("expected IOException");
            } catch (IOException expected) {
                assertTrue(expected.getMessage().contains(BINARY));
            }
        }
    }

    @Test
    public void originOfRealClassEndsWithSeparator() {
        String origin = BundledResourceResolver.originOf(BundledResourceResolver.class);
        assertNotNull(origin);
        assertTrue(origin.endsWith("/"));
    }

    // -- helpers --------------------------------------------------------------------------------------------

    /** Writes a jar with alternating (entryName, content) pairs. */
    private File jar(String name, String... entriesAndContent) throws IOException {
        File f = new File(tempDir, name);
        try (JarOutputStream out = new JarOutputStream(new FileOutputStream(f))) {
            for (int i = 0; i < entriesAndContent.length; i += 2) {
                out.putNextEntry(new JarEntry(entriesAndContent[i]));
                out.write(entriesAndContent[i + 1].getBytes(StandardCharsets.UTF_8));
                out.closeEntry();
            }
        }
        return f;
    }

    /** Parent-less loader so only the given jars are consulted, in the given order. */
    private static URLClassLoader loader(File... jars) throws IOException {
        URL[] urls = new URL[jars.length];
        for (int i = 0; i < jars.length; i++) {
            urls[i] = jars[i].toURI().toURL();
        }
        return new URLClassLoader(urls, null);
    }

    /**
     * The resolver only needs a Class whose name maps to ANCHOR_CLASS and whose classloader is {@code cl}. We
     * can't define a class from the dummy bytes, so use a real class loaded through a wrapper that reports
     * {@code cl}: a URLClassLoader child that delegates class loading to the test loader but resource lookups to
     * {@code cl}.
     */
    private static Class<?> anchorIn(URLClassLoader cl) throws Exception {
        return new AnchorLoader(cl).loadClass(KinesisProducer.class.getName());
    }

    private static Class<?> loadDummyAnchor(URLClassLoader cl) throws Exception {
        return new AnchorLoader(cl).loadClass(KinesisProducer.class.getName());
    }

    /** Loads KinesisProducer's bytes from the test classpath but answers resource queries from {@code resources}. */
    private static final class AnchorLoader extends ClassLoader {
        private final ClassLoader resources;

        AnchorLoader(ClassLoader resources) {
            super(null);
            this.resources = resources;
        }

        @Override
        protected Class<?> findClass(String name) throws ClassNotFoundException {
            if (!name.equals(KinesisProducer.class.getName())) {
                return BundledResourceResolverTest.class.getClassLoader().loadClass(name);
            }
            String path = name.replace('.', '/') + ".class";
            try (InputStream in = BundledResourceResolverTest.class.getClassLoader().getResourceAsStream(path)) {
                byte[] bytes = IOUtils.toByteArray(in);
                return defineClass(name, bytes, 0, bytes.length);
            } catch (IOException e) {
                throw new ClassNotFoundException(name, e);
            }
        }

        @Override
        public URL getResource(String name) {
            return resources.getResource(name);
        }

        @Override
        public java.util.Enumeration<URL> getResources(String name) throws IOException {
            return resources.getResources(name);
        }
    }

    private static String read(URL url) throws IOException {
        try (InputStream in = url.openStream()) {
            return IOUtils.toString(in, StandardCharsets.UTF_8);
        }
    }
}
