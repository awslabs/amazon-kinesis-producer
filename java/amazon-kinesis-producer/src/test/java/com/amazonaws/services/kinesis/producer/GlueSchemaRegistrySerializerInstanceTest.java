package com.amazonaws.services.kinesis.producer;

import com.amazonaws.services.schemaregistry.common.configs.GlueSchemaRegistryConfiguration;
import com.amazonaws.services.schemaregistry.serializers.GlueSchemaRegistrySerializer;
import org.junit.Test;
import java.util.Properties;

import static com.amazonaws.services.kinesis.producer.TestHelper.writeFile;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

public class GlueSchemaRegistrySerializerInstanceTest {

    private static final String REGION = "us-west-1";
    private GlueSchemaRegistrySerializerInstance glueSchemaRegistrySerializerInstance = new GlueSchemaRegistrySerializerInstance();

    @Test
    public void testGet_Returns_SingletonInstance() {
        KinesisProducerConfiguration configuration = createKPLConfiguration();

        GlueSchemaRegistrySerializer serializer1 =
            glueSchemaRegistrySerializerInstance.get(configuration);

        assertNotNull(serializer1);
        GlueSchemaRegistrySerializer serializer2 =
            glueSchemaRegistrySerializerInstance.get(configuration);

        assertEquals(serializer1.hashCode(), serializer2.hashCode());
    }

    @Test
    public void testGet_Returns_WhenGlueConfigurationIsExplicitlyConfigured() {
        KinesisProducerConfiguration configuration = createKPLConfiguration();

        GlueSchemaRegistrySerializer serializer =
            glueSchemaRegistrySerializerInstance.get(configuration);

        assertNotNull(serializer);
    }

    private KinesisProducerConfiguration createKPLConfiguration() {
        Properties property = new Properties();
        property.put("region", REGION);
        String glueSchemaRegistryPropertyFilePath = writeFile(property);

        Properties kinesisProducerProperties = new Properties();
        kinesisProducerProperties.put("GlueSchemaRegistryPropertiesFilePath", glueSchemaRegistryPropertyFilePath);
        return KinesisProducerConfiguration.fromPropertiesFile(writeFile(kinesisProducerProperties));
    }
}