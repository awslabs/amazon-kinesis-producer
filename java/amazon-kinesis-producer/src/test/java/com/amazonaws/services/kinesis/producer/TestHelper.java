package com.amazonaws.services.kinesis.producer;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileWriter;
import java.util.Properties;
import java.util.UUID;
import org.apache.commons.io.FileUtils;

public class TestHelper {
    public static String writeFile(String contents) {
        try {
            File tmpFile = File.createTempFile(UUID.randomUUID().toString(), ".tmp");
            tmpFile.deleteOnExit();

            FileWriter writer = new FileWriter(tmpFile);
            writer.write(contents);
            writer.close();

            return tmpFile.getAbsolutePath();
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    public static String writeFile(Properties p) {
        try {
            ByteArrayOutputStream byteOutStream = new ByteArrayOutputStream();
            p.store(byteOutStream, "");
            byteOutStream.close();
            return writeFile(byteOutStream.toString("UTF-8"));
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }
}
