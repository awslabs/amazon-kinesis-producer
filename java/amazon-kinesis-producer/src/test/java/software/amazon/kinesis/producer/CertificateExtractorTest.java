/*
 * Copyright 2019 Amazon.com, Inc. or its affiliates.
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

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.file.Files;
import java.nio.file.StandardOpenOption;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import org.apache.commons.io.Charsets;
import org.apache.commons.io.IOUtils;
import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.junit.matchers.JUnitMatchers.hasItem;

public class CertificateExtractorTest {

    @Test
    public void testNoCertificatesExtraction() throws Exception {
        File tempDirectory = Files.createTempDirectory("kpl-ca-test").toFile();
        CertificateExtractor extractor = new CertificateExtractor();
        extractor.extractCertificates(tempDirectory);

        File caCertsDirectory = new File(tempDirectory, CertificateExtractor.CA_CERTS_DIRECTORY_NAME);
        verifyAllCertificates(caCertsDirectory, extractor);

    }

    @Test
    public void testDirectoryExistsOnExtractionWithMismatch() throws Exception {
        File tempDirectory = Files.createTempDirectory("kpl-ca-test").toFile();
        File caDirectory = new File(tempDirectory, CertificateExtractor.CA_CERTS_DIRECTORY_NAME);
        assertTrue(caDirectory.mkdirs());
        File existingCert = new File(caDirectory, CertificateExtractor.CERTIFICATE_FILES.get(5));

        try (FileOutputStream fos = new FileOutputStream(existingCert)) {
            fos.write(new byte[] { 1, 2, 3, 4 });
        }

        CertificateExtractor extractor = new CertificateExtractor();
        extractor.extractCertificates(tempDirectory);

        verifyAllCertificates(caDirectory, extractor);
    }

    private void verifyAllCertificates(File caCertsDirectory, CertificateExtractor extractor) throws IOException {
        ClassLoader classLoader = CertificateExtractor.class.getClassLoader();

        Set<File> extractedCertificates = new HashSet<>(extractor.getExtractedCertificates());
        assertEquals(extractedCertificates.size(), extractor.getExtractedCertificates().size());

        for (String expectedCert : CertificateExtractor.CERTIFICATE_FILES) {
            File resourceFile = new File(CertificateExtractor.CA_CERTS_DIRECTORY_NAME, expectedCert);

            InputStream is = classLoader.getResourceAsStream(resourceFile.getPath());
            assertNotNull(is);
            List<String> expectedCertLines = IOUtils.readLines(is, Charsets.UTF_8);

            File actualCaFile = new File(caCertsDirectory, expectedCert);
            List<String> actualCertLines = Files.readAllLines(actualCaFile.toPath(), Charsets.UTF_8);

            assertThat(extractedCertificates, hasItem(actualCaFile.getAbsoluteFile()));
            assertEquals(expectedCertLines, actualCertLines);
        }
    }

    private static class LockHolder implements Runnable {

        final File lockFile;

        private LockHolder(File tempDirectory) {
            this.lockFile = new File(tempDirectory, "cacerts.lock");
        }

        @Override
        public void run() {
            holdLock();
        }

        private void holdLock() {
            try {
                try (FileLock lock = FileChannel.open(lockFile.toPath(), StandardOpenOption.CREATE, StandardOpenOption.WRITE).lock()) {
                    Thread.sleep(5000);
                }

            } catch (Exception ex) {
                throw new RuntimeException(ex);
            }
        }
    }

}