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
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.channels.FileLock;
import java.security.DigestInputStream;
import java.security.DigestOutputStream;
import java.security.MessageDigest;
import java.util.Arrays;

import org.apache.commons.io.IOUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class HashedFileCopier {
    private static final Logger log = LoggerFactory.getLogger(HashedFileCopier.class);

    static final String MESSAGE_DIGEST_ALGORITHM = "SHA-1";
    static final String TEMP_PREFIX = "kpl";
    static final String TEMP_SUFFIX = ".tmp";
    static final String LOCK_SUFFIX = ".lock";

    public static File copyFileFrom(InputStream sourceData, File destinationDirectory, String fileNameFormat)
            throws Exception {
        File tempFile = null;
        try {
            tempFile = File.createTempFile(TEMP_PREFIX, TEMP_SUFFIX, destinationDirectory);
            log.debug("Extracting file with format {}", fileNameFormat);
            FileOutputStream fileOutputStream = new FileOutputStream(tempFile);

            DigestOutputStream digestOutputStream = new DigestOutputStream(fileOutputStream,
                    MessageDigest.getInstance(MESSAGE_DIGEST_ALGORITHM));
            IOUtils.copy(sourceData, digestOutputStream);
            digestOutputStream.close();
            byte[] digest = digestOutputStream.getMessageDigest().digest();
            log.debug("Calculated digest of new file: {}", Arrays.toString(digest));
            String digestHex = BinaryToHexConverter.convert(digest);
            File finalFile = new File(destinationDirectory, String.format(fileNameFormat, digestHex));
            File lockFile = new File(destinationDirectory, String.format(fileNameFormat + LOCK_SUFFIX, digestHex));
            log.debug("Preparing to check and copy {} to {}", tempFile.getAbsolutePath(), finalFile.getAbsolutePath());
            try (FileOutputStream lockFOS = new FileOutputStream(lockFile);
                    FileLock lock = lockFOS.getChannel().lock()) {
                if (finalFile.exists() && finalFile.length() == tempFile.length()) {
                    byte[] existingFileDigest = null;
                    try (DigestInputStream digestInputStream = new DigestInputStream(new FileInputStream(finalFile),
                            MessageDigest.getInstance(MESSAGE_DIGEST_ALGORITHM))) {
                        byte[] discardedBytes = new byte[8192];
                        while (digestInputStream.read(discardedBytes) != -1) {
                            //
                            // This is just used for the side affect of the digest input stream
                            //
                        }
                        existingFileDigest = digestInputStream.getMessageDigest().digest();
                    }
                    if (Arrays.equals(digest, existingFileDigest)) {
                        //
                        // The existing file matches the expected file, it's ok to just drop out now
                        //
                        log.info("'{}' already exists, and matches.  Not overwriting.", finalFile.getAbsolutePath());
                        return finalFile;
                    }
                    log.warn(
                            "Detected a mismatch between the existing file, and the new file.  "
                                    + "Will overwrite the existing file. " + "Existing: {} -- New File: {}",
                            Arrays.toString(existingFileDigest), Arrays.toString(digest));
                }

                if (!tempFile.renameTo(finalFile)) {
                    log.error("Failed to rename '{}' to '{}'", tempFile.getAbsolutePath(), finalFile.getAbsolutePath());
                    throw new IOException("Failed to rename extracted file");
                }
            }
            return finalFile;
        } finally {
            if (tempFile != null && tempFile.exists()) {
                if (!tempFile.delete()) {
                    log.warn("Unable to delete temp file: {}", tempFile.getAbsolutePath());
                }
            }
        }
    }
}
