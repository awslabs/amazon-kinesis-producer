package software.amazon.kinesis.producer;

import com.amazonaws.services.schemaregistry.common.configs.GlueSchemaRegistryConfiguration;
import com.amazonaws.services.schemaregistry.serializers.GlueSchemaRegistrySerializer;
import com.amazonaws.services.schemaregistry.serializers.GlueSchemaRegistrySerializerImpl;

/**
 * Creates a lazy loaded Glue Schema Registry Serializer (GSR-Serializer) instance.
 * We only want to initialize the instance if there are UserRecords with schema attribute set.
 */
public final class GlueSchemaRegistrySerializerInstance {

    private volatile GlueSchemaRegistrySerializer instance = null;
    private static final String USER_AGENT_APP_NAME = "kpl-1.0.0";

    /**
     * Instantiate GlueSchemaRegistrySerializer using the KinesisProducerConfiguration.
     * If the {@link GlueSchemaRegistryConfiguration} is set, it will be used.
     * If not, the region from {@link KinesisProducerConfiguration} is used to create one.
     * @param configuration {@link KinesisProducerConfiguration}
     * @return GlueSchemaRegistrySerializer
     */
    public GlueSchemaRegistrySerializer get(KinesisProducerConfiguration configuration) {
        GlueSchemaRegistrySerializer local = instance;
        if (local == null) {
            synchronized (this) {
                local = instance;
                if (local == null) {
                    GlueSchemaRegistryConfiguration config = getConfigFromKinesisProducerConfig(configuration);
                    local =
                        new GlueSchemaRegistrySerializerImpl(
                            configuration.getGlueSchemaRegistryCredentialsProvider(),
                            config);
                    instance = local;
                }
            }
        }
        return instance;
    }

    private GlueSchemaRegistryConfiguration getConfigFromKinesisProducerConfig(
        KinesisProducerConfiguration configuration) {
        GlueSchemaRegistryConfiguration glueSchemaRegistryConfiguration = configuration.getGlueSchemaRegistryConfiguration();
        if (glueSchemaRegistryConfiguration == null) {
            //Reuse the region from KinesisProducerConfiguration.
            glueSchemaRegistryConfiguration = new GlueSchemaRegistryConfiguration(configuration.getRegion());
        }

        glueSchemaRegistryConfiguration.setUserAgentApp(USER_AGENT_APP_NAME);

        return glueSchemaRegistryConfiguration;
    }
}
