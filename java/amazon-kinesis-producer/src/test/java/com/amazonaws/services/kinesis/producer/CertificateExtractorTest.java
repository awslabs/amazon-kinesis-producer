/*
 * Copyright 2017 Amazon.com, Inc. or its affiliates. All Rights Reserved. Licensed under the Amazon Software License
 * (the "License"). You may not use this file except in compliance with the License. A copy of the License is located at
 * http://aws.amazon.com/asl/ or in the "license" file accompanying this file. This file is distributed on an "AS IS"
 * BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 */
package com.amazonaws.services.kinesis.producer;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.file.Files;
import java.nio.file.StandardOpenOption;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import org.apache.commons.io.Charsets;
import org.apache.commons.io.IOUtils;
import org.hamcrest.Matchers;
import org.junit.Test;

public class CertificateExtractorTest {

    static final List<String> EXPECTED_CERTIFICATES = Arrays.asList("062cdee6.0", "09789157.0", "116bf586.0",
            "1d3472b9.0", "244b5494.0", "2c543cd1.0", "2e4eed3c.0", "3513523f.0", "480720ec.0", "4a6481c9.0",
            "4bfab552.0", "5ad8a5d6.0", "607986c7.0", "653b494a.0", "6d41d539.0", "75d1b2ed.0", "76cb8f92.0",
            "7d0b38bd.0", "7f3d5d1d.0", "8867006a.0", "8cb5ee0f.0", "9d04f354.0", "ad088e1d.0", "b0e59380.0",
            "b1159c4c.0", "b204d74a.0", "ba89ed3b.0", "c01cdfa2.0", "c089bbbd.0", "c0ff1f52.0", "cbeee9e2.0",
            "cbf06781.0", "ce5e74ef.0", "dd8e9d41.0", "de6d66f3.0", "e2799e36.0", "f081611a.0", "f387163d.0");

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
        assertThat(caDirectory.mkdirs(), equalTo(true));
        File existingCert = new File(caDirectory, EXPECTED_CERTIFICATES.get(5));

        try (FileOutputStream fos = new FileOutputStream(existingCert)) {
            fos.write(new byte[]{1, 2, 3, 4});
        }

        CertificateExtractor extractor = new CertificateExtractor();
        extractor.extractCertificates(tempDirectory);

        verifyAllCertificates(caDirectory, extractor);
    }

    private void verifyAllCertificates(File caCertsDirectory, CertificateExtractor extractor) throws IOException {
        ClassLoader classLoader = CertificateExtractor.class.getClassLoader();

        Set<File> extractedCertificates = new HashSet<>(extractor.getExtractedCertificates());

        assertThat(extractedCertificates.size(), equalTo(extractor.getExtractedCertificates().size()));

        for (String expectedCert : EXPECTED_CERTIFICATES) {
            File resourceFile = new File(CertificateExtractor.CA_CERTS_DIRECTORY_NAME, expectedCert);

            InputStream is = classLoader.getResourceAsStream(resourceFile.getPath());
            assertThat(is, notNullValue());
            List<String> expectedCertLines = IOUtils.readLines(is, Charsets.UTF_8);

            File actualCaFile = new File(caCertsDirectory, expectedCert);
            List<String> actualCertLines = Files.readAllLines(actualCaFile.toPath(), Charsets.UTF_8);

            assertThat(extractedCertificates, Matchers.hasItem(actualCaFile.getAbsoluteFile()));

            assertThat(actualCertLines, equalTo(expectedCertLines));
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