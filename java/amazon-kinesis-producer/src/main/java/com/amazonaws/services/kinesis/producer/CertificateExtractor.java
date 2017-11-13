/*
 * Copyright 2017 Amazon.com, Inc. or its affiliates. All Rights Reserved. Licensed under the Amazon Software License
 * (the "License"). You may not use this file except in compliance with the License. A copy of the License is located at
 * http://aws.amazon.com/asl/ or in the "license" file accompanying this file. This file is distributed on an "AS IS"
 * BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 */
package com.amazonaws.services.kinesis.producer;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.io.IOException;
import java.net.URISyntaxException;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;
import java.nio.file.FileSystem;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.BasicFileAttributes;
import java.security.CodeSource;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

class CertificateExtractor {

    static final String CA_CERTS_DIRECTORY_NAME = "cacerts";
    static final String LOCK_FILE_NAME = CA_CERTS_DIRECTORY_NAME + ".lock";

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
                    extractCertificatesUnderLock(destinationCaDirectory);
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

    private void extractCertificatesUnderLock(File destinationCaDirectory) throws IOException {
        CodeSource codeSource = certificateSourceClass.getProtectionDomain().getCodeSource();
        if (codeSource != null) {
            FileSystem fs;
            try {
                final Path path = Paths.get(codeSource.getLocation().toURI());
                fs = path.getFileSystem();
                final Path destinationPath = destinationCaDirectory.toPath();

                Path caCertsPath = fs.getPath(path.toString(), CA_CERTS_DIRECTORY_NAME);
                Files.walkFileTree(caCertsPath, new VerifyingVisitor(destinationPath));
            } catch (URISyntaxException ex) {
                log.warn("Unable to create Path from URL: {}", codeSource.getLocation());
                throw new RuntimeException(ex);
            }
        } else {
            log.warn("Unable to get code source to extract certificates from {}.", certificateSourceClass.getName());
            throw new RuntimeException("Unable to find certificates");
        }
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

    private class VerifyingVisitor extends SimpleFileVisitor<Path> {

        private final Path destinationPath;

        private VerifyingVisitor(Path destinationPath) {
            this.destinationPath = destinationPath;
        }

        @Override
        public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) throws IOException {
            if (attrs.isRegularFile()) {
                Path destination = new File(destinationPath.toFile(), file.getFileName().toString()).toPath();
                log.debug("Extracting certificate '{}' to '{}'", file.getFileName(), destination);
                byte[] certificateData = Files.readAllBytes(file);
                if (Files.exists(destination)) {
                    byte[] existingData = Files.readAllBytes(destination);
                    if (certificateData.length == existingData.length && Arrays.equals(certificateData, existingData)) {
                        log.debug("Certificate '{}' already exists, and content matches. Skipping", file.getFileName());
                        return FileVisitResult.CONTINUE;
                    }
                    log.info("Certificate '{}' already exists, but the content doesn't match. Overwriting", file.getFileName());
                }
                Files.copy(file, destination, StandardCopyOption.REPLACE_EXISTING);
                extractedCertificates.add(destination.toFile().getAbsoluteFile());
            }
            return FileVisitResult.CONTINUE;
        }
    }
}
