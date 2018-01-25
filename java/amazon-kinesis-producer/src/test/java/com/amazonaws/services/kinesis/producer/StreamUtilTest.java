package com.amazonaws.services.kinesis.producer;

import org.apache.commons.io.IOUtils;
import org.junit.Test;

import javax.xml.bind.DatatypeConverter;
import java.io.InputStream;
import java.security.MessageDigest;

import static com.amazonaws.util.ClassLoaderHelper.getResourceAsStream;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class StreamUtilTest {

    @Test
    public void testSuccessfulStreamComparison() throws Exception {
        InputStream stream1 = getResourceAsStream("stream1.txt");
        InputStream stream2 = getResourceAsStream("stream1.txt");
        assertTrue(StreamUtil.compareStreamsChunked(stream1, stream2));
    }

    @Test
    public void testFailedStreamComparison() throws Exception {
        InputStream stream1 = getResourceAsStream("stream1.txt");
        InputStream stream2 = getResourceAsStream("stream1Truncated.txt");
        assertFalse(StreamUtil.compareStreamsChunked(stream1, stream2));
    }

    @Test
    public void testStreamMD5Creation() throws Exception {
        InputStream stream = getResourceAsStream("stream1.txt");
        String chunkedStreamMD5 = StreamUtil.getMD5(stream);

        byte[] bin = IOUtils.toByteArray(getResourceAsStream("stream1.txt"));
        MessageDigest md = MessageDigest.getInstance("SHA1");
        String byteMD5 = DatatypeConverter.printHexBinary(md.digest(bin)).toLowerCase();

        assertEquals(chunkedStreamMD5, byteMD5);
    }

}
