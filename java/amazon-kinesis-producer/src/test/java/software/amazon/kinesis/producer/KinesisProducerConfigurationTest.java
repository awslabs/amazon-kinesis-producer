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

import org.apache.commons.io.FileUtils;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import software.amazon.awssdk.auth.credentials.DefaultCredentialsProvider;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.util.Properties;
import java.util.UUID;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

public class KinesisProducerConfigurationTest {
    @SuppressWarnings("unused")
    private static final Logger log = LoggerFactory.getLogger(KinesisProducerConfigurationTest.class);

    private static String writeFile(String contents) {
        try {
            File f = File.createTempFile(UUID.randomUUID().toString(), "");
            f.deleteOnExit();
            FileUtils.write(f, contents);
            return f.getAbsolutePath();
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    private static String writeFile(Properties p) {
        try {
            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            p.store(baos, "");
            baos.close();
            return writeFile(new String(baos.toByteArray(), "UTF-8"));
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    @Test
    public void loadString() {
        Properties p = new Properties();
        String v = UUID.randomUUID().toString();
        p.setProperty("MetricsNamespace", v);
        KinesisProducerConfiguration cfg = KinesisProducerConfiguration.fromPropertiesFile(writeFile(p));
        assertEquals(v, cfg.getMetricsNamespace());
    }

    @Test
    public void loadLong() {
        KinesisProducerConfiguration defaultConfig = new KinesisProducerConfiguration();
        Properties p = new Properties();
        long v = defaultConfig.getConnectTimeout() + 1;
        p.setProperty("ConnectTimeout", Long.toString(v));
        KinesisProducerConfiguration cfg = KinesisProducerConfiguration.fromPropertiesFile(writeFile(p));
        assertEquals(v, cfg.getConnectTimeout());
    }

    @Test
    public void loadBoolean() {
        KinesisProducerConfiguration defaultConfig = new KinesisProducerConfiguration();
        Properties p = new Properties();
        boolean v = !defaultConfig.isVerifyCertificate();
        p.setProperty("VerifyCertificate", Boolean.toString(v));
        KinesisProducerConfiguration cfg = KinesisProducerConfiguration.fromPropertiesFile(writeFile(p));
        assertEquals(v, cfg.isVerifyCertificate());
    }

    @Test
    public void unknownProperty() {
        Properties p = new Properties();
        p.setProperty("xcdfndetnedtne5tje45", "Sfbsfrne34534");
        KinesisProducerConfiguration.fromPropertiesFile(writeFile(p));
        // should not throw exception
    }

    @Test
    public void setThreadingModelToPooledFromProperties() {
        Properties p = new Properties();
        p.setProperty("ThreadingModel", "POOLED");
        KinesisProducerConfiguration cfg = KinesisProducerConfiguration.fromPropertiesFile(writeFile(p));
        assertEquals(KinesisProducerConfiguration.ThreadingModel.POOLED, cfg.getThreadingModel());
    }

    @Test
    public void setThreadingModelToPerRequestFromProperties() {
        Properties p = new Properties();
        p.setProperty("ThreadingModel", "PER_REQUEST");
        KinesisProducerConfiguration cfg = KinesisProducerConfiguration.fromPropertiesFile(writeFile(p));
        assertEquals(KinesisProducerConfiguration.ThreadingModel.PER_REQUEST, cfg.getThreadingModel());
    }

    @Test
    public void setThreadPoolSizeFromProperties() {
        Properties p = new Properties();
        int v = 4;
        p.setProperty("ThreadPoolSize", Integer.toString(v));
        KinesisProducerConfiguration cfg = KinesisProducerConfiguration.fromPropertiesFile(writeFile(p));
        assertEquals(v, cfg.getThreadPoolSize());
    }

    @Test
    public void setGlueSchemaRegistryConfigFromProperties() {
        String region = "ca-central-1";
        Properties schemaRegistryProperties = new Properties();
        schemaRegistryProperties.put("region", region);
        String glueSchemaRegistryPropertyFilePath = writeFile(schemaRegistryProperties);

        Properties kinesisProducerProperties = new Properties();
        kinesisProducerProperties.put("GlueSchemaRegistryPropertiesFilePath", glueSchemaRegistryPropertyFilePath);
        KinesisProducerConfiguration cfg = KinesisProducerConfiguration.fromPropertiesFile(writeFile(kinesisProducerProperties));

        assertNotNull(cfg);
        assertNotNull(cfg.getGlueSchemaRegistryConfiguration());
        assertEquals(glueSchemaRegistryPropertyFilePath, cfg.getGlueSchemaRegistryPropertiesFilePath());
        assertEquals(region, cfg.getGlueSchemaRegistryConfiguration().getRegion());
    }

    @Test
    public void setGlueSchemaRegistryCredentialsProvider() {
        KinesisProducerConfiguration cfg = new KinesisProducerConfiguration();
        cfg.setGlueSchemaRegistryCredentialsProvider(DefaultCredentialsProvider.create());

        assertNotNull(cfg.getGlueSchemaRegistryCredentialsProvider());
    }
}
