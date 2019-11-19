package com.amazonaws.services.kinesis.producer.sample;

import com.amazonaws.auth.DefaultAWSCredentialsProviderChain;
import com.amazonaws.services.kinesis.producer.KinesisProducerConfiguration;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class SampleProducerConfig {
    
    private static final Logger log = LoggerFactory.getLogger(SampleProducerConfig.class);
    /**
     * Change these to try larger or smaller records.
     */
    private static final int DATA_SIZE_DEFAULT = 128;

    /**
     * Put records for this number of seconds before exiting.
     */
    private static final int SECONDS_TO_RUN_DEFAULT = 5;

    /**
     * Put this number of records per second.
     *
     * Because multiple logical records are combined into each Kinesis record,
     * even a single shard can handle several thousand records per second, even
     * though there is a limit of 1000 Kinesis records per shard per second.
     *
     * If a shard gets throttled, the KPL will continue to retry records until
     * either they succeed or reach a TTL set in the KPL's configuration, at
     * which point the KPL will return failures for those records.
     *
     * @see {@link KinesisProducerConfiguration#setRecordTtl(long)}
     */
    private static final int RECORDS_PER_SECOND_DEFAULT = 2000;

    /**
     * Change this to your stream name.
     */
    public static final String STREAM_NAME_DEFAULT = "test";

    /**
     * Change this to the region you are using.
     */
    public static final String REGION_DEFAULT = "us-west-1";

    private final String region;
    private final int connections;
    private final int requestTimeout;
    private final int bufferTime;
    private final String threadingModel;
    private final String streamName;
    private final int secondsToRun;
    private final int recordsPerSecond;
    private final int dataSize;
    private final int threadPoolSize;

    public static String getArgIfPresent(final String[] args, final int index, final String defaultValue) {
        return args.length > index ? args[index] : defaultValue;
    }

    /**
     * Parses the commandline input to produce a Config object
     **  @param args  The command line args for the Sample Producer. It takes 3 optional position parameters:
     *      *  1. The stream name to use (test is default)
     *      *  2. The region name to use (us-west-1 in default)
     *      *  3. The duration of the test in seconds, 5 is the default.
     *      *  4. The number of records per second to send, 2000 is the default.
     *      *  5. The payload size of each record being sent in bytes, 128 is the default.
     *      *  6. The max number of connections to configure the KPL with, 1 is the default.
     *      *  7. The requestTimeout in milliseconds to configure the KPL with, 60000 is the default.
     *      *  8. The bufferTime in milliseconds to configure the KPL with, 2000 is the default.
     *      *  9. The threading model to configure the KPL with, PER_REQUEST is the default.
     *      *  10. The threadPoolSize to configure the KPL with, 0 is the default.
     */
    public SampleProducerConfig(String[] args) {

        int argIndex = 0;
        streamName = getArgIfPresent(args, argIndex++, STREAM_NAME_DEFAULT);
        region = getArgIfPresent(args, argIndex++, REGION_DEFAULT);
        secondsToRun = Integer.parseInt(getArgIfPresent(args, argIndex++, String.valueOf(SECONDS_TO_RUN_DEFAULT)));
        recordsPerSecond = Integer.parseInt(getArgIfPresent(args, argIndex++, String.valueOf(RECORDS_PER_SECOND_DEFAULT)));
        dataSize = Integer.parseInt(getArgIfPresent(args, argIndex++, String.valueOf(DATA_SIZE_DEFAULT)));
        connections = Integer.parseInt(getArgIfPresent(args, argIndex++, String.valueOf(1)));
        requestTimeout = Integer.parseInt(getArgIfPresent(args, argIndex++, String.valueOf(60000)));
        bufferTime = Integer.parseInt(getArgIfPresent(args, argIndex++, String.valueOf(2000)));
        threadingModel = getArgIfPresent(args, argIndex++, KinesisProducerConfiguration.ThreadingModel.PER_REQUEST.name());
        threadPoolSize = Integer.parseInt(getArgIfPresent(args, argIndex++, String.valueOf(0)));

        boolean errorsFound=false;
        if (secondsToRun <= 0) {
            errorsFound=true;
            log.error("SecondsToRun should be a positive integer");
        }

        if (recordsPerSecond <= 0) {
            errorsFound=true;
            log.error("RecordsPerSecond should be a positive integer");
        }

        if (dataSize <= 0) {
            errorsFound=true;
            log.error("DataSize should be a positive integer");
        }

        if (connections <= 0) {
            errorsFound=true;
            log.error("Connections should be a positive integer");
        }

        if (requestTimeout <= 0) {
            errorsFound=true;
            log.error("RequestTimeout should be a positive integer");
        }

        if (bufferTime <= 0) {
            errorsFound=true;
            log.error("BufferTime should be a positive integer");
        }

        if (threadPoolSize < 0) {
            errorsFound=true;
            log.error("ThreadPoolSize should be a positive integer, or 0");
        }

        if(!threadingModel.equals(KinesisProducerConfiguration.ThreadingModel.PER_REQUEST.name()) &&
                !threadingModel.equals(KinesisProducerConfiguration.ThreadingModel.POOLED.name())){
            errorsFound=true;
            log.error("Threading model needs to be one of [PER_REQUEST | POOLED]");
        }

        if (threadPoolSize < 0) {
            errorsFound=true;
            log.error("ThreadPoolSize should be a positive integer, or 0");
        }

        if(errorsFound){
            System.exit(1);
        }
    }

    public String getRegion() {
        return region;
    }

    public int getConnections() {
        return connections;
    }

    public int getRequestTimeout() {
        return requestTimeout;
    }

    public int getBufferTime() {
        return bufferTime;
    }

    public String getThreadingModel() {
        return threadingModel;
    }

    public String getStreamName() {
        return streamName;
    }

    public int getSecondsToRun() {
        return secondsToRun;
    }

    public int getRecordsPerSecond() {
        return recordsPerSecond;
    }

    public int getDataSize() {
        return dataSize;
    }

    public int getThreadPoolSize() {
        return threadPoolSize;
    }

    public KinesisProducerConfiguration transformToKinesisProducerConfiguration(){
        // There are many configurable parameters in the KPL. See the javadocs
        // on each each set method for details.
        KinesisProducerConfiguration config = new KinesisProducerConfiguration();

        // You can also load config from file. A sample properties file is
        // included in the project folder.
        // KinesisProducerConfiguration config =
        //     KinesisProducerConfiguration.fromPropertiesFile("default_config.properties");

        // If you're running in EC2 and want to use the same Kinesis region as
        // the one your instance is in, you can simply leave out the region
        // configuration; the KPL will retrieve it from EC2 metadata.
        config.setRegion(this.getRegion());

        // You can pass credentials programmatically through the configuration,
        // similar to the AWS SDK. DefaultAWSCredentialsProviderChain is used
        // by default, so this configuration can be omitted if that is all
        // that is needed.
        config.setCredentialsProvider(new DefaultAWSCredentialsProviderChain());

        // The maxConnections parameter can be used to control the degree of
        // parallelism when making HTTP requests. We're going to use only 1 here
        // since our throughput is fairly low. Using a high number will cause a
        // bunch of broken pipe errors to show up in the logs. This is due to
        // idle connections being closed by the server. Setting this value too
        // large may also cause request timeouts if you do not have enough
        // bandwidth.
        config.setMaxConnections(this.getConnections());

        // Set a more generous timeout in case we're on a slow connection.
        config.setRequestTimeout(this.getRequestTimeout());

        // RecordMaxBufferedTime controls how long records are allowed to wait
        // in the KPL's buffers before being sent. Larger values increase
        // aggregation and reduces the number of Kinesis records put, which can
        // be helpful if you're getting throttled because of the records per
        // second limit on a shard. The default value is set very low to
        // minimize propagation delay, so we'll increase it here to get more
        // aggregation.
        config.setRecordMaxBufferedTime(this.getBufferTime());

        // Configures the threading model for the KPL. By default the KPL
        // launches with a PER_REQUEST threading model with a threadpool size
        // of 0. At larger stream sizes it becomes more beneficial to change
        // to a pooled threading model.
        config.setThreadingModel(this.getThreadingModel());
        config.setThreadPoolSize(this.getThreadPoolSize());

        // If you have built the native binary yourself, you can point the Java
        // wrapper to it with the NativeExecutable option. If you want to pass
        // environment variables to the executable, you can either use a wrapper
        // shell script, or set them for the Java process, which will then pass
        // them on to the child process.
        // config.setNativeExecutable("my_directory/kinesis_producer");

        // If you end up using the default configuration (a Configuration instance
        // without any calls to set*), you can just leave the config argument
        // out.
        //
        // Note that if you do pass a Configuration instance, mutating that
        // instance after initializing KinesisProducer has no effect. We do not
        // support dynamic re-configuration at the moment.

        return config;
    }
}
