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
import java.io.IOException;
import java.io.InputStream;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import org.apache.commons.io.IOUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

class CertificateExtractor {

    static final String CA_CERTS_DIRECTORY_NAME = "cacerts";
    static final String LOCK_FILE_NAME = CA_CERTS_DIRECTORY_NAME + ".lock";
    static final List<String> CERTIFICATE_FILES = Arrays.asList("062cdee6.0", "09789157.0", "116bf586.0", "1d3472b9.0",
            "244b5494.0", "2c543cd1.0", "2e4eed3c.0", "3513523f.0", "480720ec.0", "4a6481c9.0", "4bfab552.0",
            "5ad8a5d6.0", "607986c7.0", "653b494a.0", "6d41d539.0", "75d1b2ed.0", "76cb8f92.0", "7d0b38bd.0",
            "7f3d5d1d.0", "8867006a.0", "8cb5ee0f.0", "9d04f354.0", "ad088e1d.0", "b0e59380.0", "b1159c4c.0",
            "b204d74a.0", "ba89ed3b.0", "c01cdfa2.0", "c089bbbd.0", "c0ff1f52.0", "cbeee9e2.0", "cbf06781.0",
            "ce5e74ef.0", "dd8e9d41.0", "de6d66f3.0", "e2799e36.0", "f081611a.0", "f387163d.0");

    private static final Logger log = LoggerFactory.getLogger(CertificateExtractor.class);
    private final Class<?> certificateSourceClass;

    private final List<File> extractedCertificates = new ArrayList<>();

    CertificateExtractor() {
        this(CertificateExtractor.class);
    }

    CertificateExtractor(Class<?> certificateSourceClass) {
        this.certificateSourceClass = certificateSourceClass;
    }

    String extractCertificates(File tempDirectory) throws IOException {

        Path lockFile = new File(tempDirectory, LOCK_FILE_NAME).toPath();
        boolean lockHeld = false;
        int attempts = 1;
        File destinationCaDirectory = prepareDestination(tempDirectory);
        while (!lockHeld) {
            try {
                try (FileLock lock = FileChannel.open(lockFile, StandardOpenOption.CREATE, StandardOpenOption.WRITE).lock()) {
                    lockHeld = true;
                    extractAndVerifyCertificates(destinationCaDirectory);
                }
            } catch (OverlappingFileLockException ofle) {
                attempts++;
                log.info("Another thread holds the certificate lock, sleeping for 1 second and will attempt again.  Lock Attempts: {}", attempts);
                try {
                    Thread.sleep(1000);
                } catch (InterruptedException ie) {
                    log.info("Interrupted while sleeping for lock.  Giving up on certificate extraction.");
                    break;
                }
            }
        }
        return destinationCaDirectory.getAbsolutePath();
    }

    List<File> getExtractedCertificates() {
        return extractedCertificates;
    }

    private File prepareDestination(File tempDirectory) throws IOException {
        File destinationCaCerts = tempDirectory.getName().endsWith(CA_CERTS_DIRECTORY_NAME) ? tempDirectory
                : new File(tempDirectory, CA_CERTS_DIRECTORY_NAME);
        if (!destinationCaCerts.exists()) {
            if (!destinationCaCerts.mkdirs()) {
                if (!destinationCaCerts.exists()) {
                    throw new IOException(
                            "Failed to create directory for certs at '" + destinationCaCerts.getAbsolutePath() + "'");
                }
            }
        }
        return destinationCaCerts;
    }

    private void extractAndVerifyCertificates(File destinationPath) throws IOException {
        for (String certificate : CERTIFICATE_FILES) {
            InputStream certificateSource = certificateSourceClass.getClassLoader()
                    .getResourceAsStream(CA_CERTS_DIRECTORY_NAME + "/" + certificate);

            File destinationCertificate = new File(destinationPath, certificate);
            log.debug("Extracting certificate '{}' to '{}'", certificate, destinationCertificate);
            byte[] certificateData = IOUtils.toByteArray(certificateSource);
            extractedCertificates.add(destinationCertificate.getAbsoluteFile());
            if (destinationCertificate.exists()) {
                byte[] existingData = Files.readAllBytes(destinationCertificate.toPath());
                if (Arrays.equals(certificateData, existingData)) {
                    log.debug("Certificate '{}' already exists, and content matches. Skipping", certificate);
                    continue;
                }
                log.info("Certificate '{}' already exists, but the content doesn't match. Overwriting", certificate);
            }
            Files.write(destinationCertificate.toPath(), certificateData, StandardOpenOption.WRITE,
                    StandardOpenOption.CREATE, StandardOpenOption.TRUNCATE_EXISTING);
        }
    }
}
