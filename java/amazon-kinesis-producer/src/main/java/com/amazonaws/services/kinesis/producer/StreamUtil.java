package com.amazonaws.services.kinesis.producer;

import javax.xml.bind.DatatypeConverter;
import java.io.DataInputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;
import java.security.DigestInputStream;
import java.security.MessageDigest;

/**
 * Utility to perform stream operations using chunks to save on JVM memory.
 */
public class StreamUtil {

    private static final int CHUNK_SIZE = 64 * 1034;

    /**
     * Will close both streams before returning result.
     */
    public static boolean compareStreamsChunked(InputStream stream1, InputStream stream2) throws IOException {
        byte[] stream1Chunk = new byte[CHUNK_SIZE];
        byte[] stream2Chunk = new byte[CHUNK_SIZE];
        try {
            DataInputStream stream2DataStream = new DataInputStream(stream2);
            int len;
            while ((len = stream1.read(stream1Chunk)) > 0) {
                // readFully guarantees we read the same number of bytes
                // that were just read from stream 1.
                // EOFException is thrown if we are past the end of stream2.
                stream2DataStream.readFully(stream2Chunk, 0, len);
                for (int i = 0; i < len; i++) {
                    if (stream1Chunk[i] != stream2Chunk[i]) {
                        return false;
                    }
                }
            }

            // If the second stream has been completely consumed at this point
            // we know the streams are identical.
            return stream2.read() < 0;
        } catch (EOFException ioe) {
            // We reached the end of stream 2 before the end of stream 1
            // indicating the streams are not identical.
            return false;
        } finally {
            stream1.close();
            stream2.close();
        }
    }

    /**
     * Will close stream once MD5 has been calculated.
     */
    public static String getMD5(InputStream stream) throws Exception {
        byte[] buffer = new byte[CHUNK_SIZE];
        MessageDigest md = MessageDigest.getInstance("SHA-1");
        try (DigestInputStream dis = new DigestInputStream(stream, md)) {
            // Read the entire stream to be able to generate the MD5
            while (dis.read(buffer) != -1) {}
        }

        return DatatypeConverter.printHexBinary(md.digest()).toLowerCase();
    }
}
