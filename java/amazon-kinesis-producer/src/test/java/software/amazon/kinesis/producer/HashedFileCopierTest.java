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

import static org.hamcrest.CoreMatchers.equalTo;
import static org.junit.Assert.assertThat;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.Charset;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.DigestInputStream;
import java.security.MessageDigest;

import org.apache.commons.io.IOUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class HashedFileCopierTest {

    private static final Logger log = LoggerFactory.getLogger(HashedFileCopierTest.class);

    private static final String TEST_FILE_PREFIX = "res-file.";
    private static final String TEST_FILE_SUFFIX = ".txt";
    private static final String TEST_FILE_FORMAT = TEST_FILE_PREFIX + "%s" + TEST_FILE_SUFFIX;

    private File tempDir;

    @Before
    public void before() throws Exception {
        tempDir = Files.createTempDirectory("kpl-unit-tests").toFile();
    }

    @After
    public void after() throws Exception {
        int extraTempFiles = 0;
        if (tempDir != null && tempDir.isDirectory()) {
            DirectoryStream<Path> directoryStream = Files.newDirectoryStream(tempDir.toPath());
            for (Path entry : directoryStream) {
                String filename = entry.toFile().getName();
                if (filename.startsWith(HashedFileCopier.TEMP_PREFIX)
                        && filename.endsWith(HashedFileCopier.TEMP_SUFFIX)) {
                    Files.delete(entry);
                    extraTempFiles++;
                    continue;
                }
                if (filename.startsWith(TEST_FILE_PREFIX) && filename.endsWith(TEST_FILE_SUFFIX)) {
                    Files.delete(entry);
                    continue;
                }
                if (filename.startsWith(TEST_FILE_PREFIX) && filename.endsWith(HashedFileCopier.LOCK_SUFFIX)) {
                    Files.delete(entry);
                    continue;
                }
                log.warn("Unexpected file {} found. Not deleting the file.", entry);
            }
            Files.delete(tempDir.toPath());
        }

        assertThat("Copier didn't clean up all temporary files.", extraTempFiles, equalTo(0));
    }

    @Test
    public void normalFileCopyTest() throws Exception {

        File resultFile = HashedFileCopier.copyFileFrom(testDataInputStream(), tempDir, TEST_FILE_FORMAT);
        File expectedFile = makeTestFile();

        assertThat(resultFile, equalTo(expectedFile));
        assertThat(expectedFile.exists(), equalTo(true));

        byte[] writtenBytes = Files.readAllBytes(resultFile.toPath());
        byte[] expectedBytes = IOUtils.toByteArray(testDataInputStream());

        assertThat(writtenBytes, equalTo(expectedBytes));

    }

    @Test
    public void fileExistsTest() throws Exception {
        File expectedFile = makeTestFile();
        try (FileOutputStream fso = new FileOutputStream(expectedFile)) {
            IOUtils.copy(testDataInputStream(), fso);
        }
        File resultFile = HashedFileCopier.copyFileFrom(testDataInputStream(), tempDir, TEST_FILE_FORMAT);
        assertThat(resultFile, equalTo(expectedFile));

        byte[] expectedData = testDataBytes();
        byte[] actualData = Files.readAllBytes(resultFile.toPath());

        assertThat(actualData, equalTo(expectedData));
    }

    @Test
    public void lengthMismatchTest() throws Exception {
        File expectedFile = makeTestFile();
        FileOutputStream fso = new FileOutputStream(expectedFile);
        IOUtils.copy(testDataInputStream(), fso);
        fso.write("This is some extra crap".getBytes(Charset.forName("UTF-8")));
        fso.close();

        File resultFile = HashedFileCopier.copyFileFrom(testDataInputStream(), tempDir, TEST_FILE_FORMAT);
        assertThat(resultFile, equalTo(expectedFile));

        byte[] expectedData = testDataBytes();
        byte[] actualData = Files.readAllBytes(resultFile.toPath());

        assertThat(actualData, equalTo(expectedData));
    }

    @Test
    public void hashMismatchTest() throws Exception {
        File expectedFile = makeTestFile();
        byte[] testData = testDataBytes();
        testData[10] = (byte)~testData[10];

        Files.write(expectedFile.toPath(), testData);

        File resultFile = HashedFileCopier.copyFileFrom(testDataInputStream(), tempDir, TEST_FILE_FORMAT);
        assertThat(resultFile, equalTo(expectedFile));

        byte[] expectedData = testDataBytes();
        byte[] actualData = Files.readAllBytes(resultFile.toPath());

        assertThat(actualData, equalTo(expectedData));
    }

    private File makeTestFile() throws Exception {
        return new File(tempDir, String.format(TEST_FILE_FORMAT, hexDigestForTestData()));
    }

    private String hexDigestForTestData() throws Exception {
        return BinaryToHexConverter.convert(hashForTestData());
    }

    private byte[] testDataBytes() throws Exception {
        return IOUtils.toByteArray(testDataInputStream());
    }

    private byte[] hashForTestData() throws Exception {
        DigestInputStream dis = new DigestInputStream(testDataInputStream(), MessageDigest.getInstance(HashedFileCopier.MESSAGE_DIGEST_ALGORITHM));
        IOUtils.toByteArray(dis);
        return dis.getMessageDigest().digest();
    }

    private InputStream testDataInputStream() {
        return this.getClass().getClassLoader().getResourceAsStream("test-data/test.txt");
    }
}