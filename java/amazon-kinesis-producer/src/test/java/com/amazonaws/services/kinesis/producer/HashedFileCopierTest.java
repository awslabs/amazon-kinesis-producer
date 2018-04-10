/*
 *  Copyright 2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 *  Licensed under the Amazon Software License (the "License").
 *  You may not use this file except in compliance with the License.
 *  A copy of the License is located at
 *
 *  http://aws.amazon.com/asl/
 *
 *  or in the "license" file accompanying this file. This file is distributed
 *  on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 *  express or implied. See the License for the specific language governing
 *  permissions and limitations under the License.
 */
package com.amazonaws.services.kinesis.producer;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.junit.Assert.assertThat;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.security.DigestInputStream;
import java.security.MessageDigest;

import javax.xml.bind.DatatypeConverter;

import org.apache.commons.io.IOUtils;
import org.junit.Before;
import org.junit.Test;

public class HashedFileCopierTest {

    private File tempDir;

    @Before
    public void before() throws Exception {
        tempDir = Files.createTempDirectory("kpl-unit-tests").toFile();
    }

    @Test
    public void normalFileCopyTest() throws Exception {

        File resultFile = HashedFileCopier.copyFileFrom(testDataInputStream(), tempDir, "res-file.%s.txt");
        File expectedFile = new File(tempDir, "res-file." + hexDigestForTestData() + ".txt");

        assertThat(resultFile, equalTo(expectedFile));
        assertThat(expectedFile.exists(), equalTo(true));

        byte[] writtenBytes = Files.readAllBytes(resultFile.toPath());
        byte[] expectedBytes = IOUtils.toByteArray(testDataInputStream());

        assertThat(writtenBytes, equalTo(expectedBytes));

    }

    @Test
    public void fileExistsTest() throws Exception {
        String executableFormat = "res-file.%s.txt";
        File expectedFile = new File(tempDir, String.format(executableFormat, hexDigestForTestData()));
        try (FileOutputStream fso = new FileOutputStream(expectedFile)) {
            IOUtils.copy(testDataInputStream(), fso);
        }
        File resultFile = HashedFileCopier.copyFileFrom(testDataInputStream(), tempDir, executableFormat);
        assertThat(resultFile, equalTo(expectedFile));

        byte[] expectedData = testDataBytes();
        byte[] actualData = Files.readAllBytes(resultFile.toPath());

        assertThat(actualData, equalTo(expectedData));
    }

    @Test
    public void lengthMismatchTest() throws Exception {
        String executableFormat = "res-file.%s.txt";
        File expectedFile = new File(tempDir, String.format(executableFormat, hexDigestForTestData()));
        FileOutputStream fso = new FileOutputStream(expectedFile);
        IOUtils.copy(testDataInputStream(), fso);
        fso.write("This is some extra crap".getBytes(Charset.forName("UTF-8")));
        fso.close();

        File resultFile = HashedFileCopier.copyFileFrom(testDataInputStream(), tempDir, executableFormat);
        assertThat(resultFile, equalTo(expectedFile));

        byte[] expectedData = testDataBytes();
        byte[] actualData = Files.readAllBytes(resultFile.toPath());

        assertThat(actualData, equalTo(expectedData));
    }

    @Test
    public void hashMismatchTest() throws Exception {
        String executableFormat = "res-file.%s.txt";
        File expectedFile = new File(tempDir, String.format(executableFormat, hexDigestForTestData()));
        byte[] testData = testDataBytes();
        testData[10] = (byte)~testData[10];

        Files.write(expectedFile.toPath(), testData);

        File resultFile = HashedFileCopier.copyFileFrom(testDataInputStream(), tempDir, executableFormat);
        assertThat(resultFile, equalTo(expectedFile));

        byte[] expectedData = testDataBytes();
        byte[] actualData = Files.readAllBytes(resultFile.toPath());

        assertThat(actualData, equalTo(expectedData));
    }

    private String hexDigestForTestData() throws Exception {
        return DatatypeConverter.printHexBinary(hashForTestData());
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