// Copyright 2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Amazon Software License (the "License").
// You may not use this file except in compliance with the License.
// A copy of the License is located at
//
//  http://aws.amazon.com/asl
//
// or in the "license" file accompanying this file. This file is distributed
// on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied. See the License for the specific language governing
// permissions and limitations under the License.

package com.amazonaws.services.kinesis.producer;

import java.io.FileInputStream;
import java.io.InputStream;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.List;
import java.util.Properties;
import java.util.regex.Pattern;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.amazonaws.auth.AWSCredentialsProvider;
import com.amazonaws.auth.DefaultAWSCredentialsProviderChain;
import com.amazonaws.services.kinesis.producer.protobuf.Config.AdditionalDimension;
import com.amazonaws.services.kinesis.producer.protobuf.Config.Configuration;
import com.amazonaws.services.kinesis.producer.protobuf.Messages.Message;

/**
 * Configuration for {@link KinesisProducer}. See each each individual set
 * method for details about each parameter.
 */
public class KinesisProducerConfiguration {
    private static final Logger log = LoggerFactory.getLogger(KinesisProducerConfiguration.class);

    private List<AdditionalDimension> additionalDims = new ArrayList<>();
    private AWSCredentialsProvider credentialsProvider = new DefaultAWSCredentialsProviderChain();
    private AWSCredentialsProvider metricsCredentialsProvider = null;

   /**
     * Add an additional, custom dimension to the metrics emitted by the KPL.
     *
     * <p>
     * For example, you can make the KPL emit per-host metrics by adding
     * HostName as the key and the domain name of the current host as the value.
     *
     * <p>
     * The granularity of the custom dimension must be specified with the
     * granularity parameter. The options are "global", "stream" and "shard",
     * just like {@link #setMetricsGranularity(String)}. If global is chosen,
     * the custom dimension will be inserted before the stream name; if stream
     * is chosen then the custom metric will be inserted after the stream name,
     * but before the shard id. Lastly, if shard is chosen, the custom metric is
     * inserted after the shard id.
     *
     * <p>
     * For example, if you want to see how different hosts are affecting a
     * single stream, you can choose a granularity of stream for your HostName
     * custom dimension. This will produce per-host metrics for every stream. On
     * the other hand, if you want to see how a single host is distributing its
     * load across different streams, you can choose a granularity of global.
     * This will produce per-stream metrics for each host.
     *
     * <p>
     * Note that custom dimensions will multiplicatively increase the number of
     * metrics emitted by the KPL into CloudWatch.
     *
     * @param key
     *            Name of the dimension, e.g. "HostName". Length must be between
     *            1 and 255.
     * @param value
     *            Value of the dimension, e.g. "my-host-1.my-domain.com". Length
     *            must be between 1 and 255.
     * @param granularity
     *            Granularity of the custom dimension, must be one of "global",
     *            "stream" or "shard"
     * @throws IllegalArgumentException
     *             If granularity is not one of the allowed values.
     */
    public void addAdditionalMetricsDimension(String key, String value, String granularity) {
        if (!Pattern.matches("global|stream|shard", granularity)) {
            throw new IllegalArgumentException("level must match the pattern global|stream|shard, got " + granularity);
        }
        additionalDims.add(AdditionalDimension.newBuilder().setKey(key).setValue(value).setGranularity(granularity).build());
    }
    
    /**
     * {@link AWSCredentialsProvider} that supplies credentials used to put
     * records to Kinesis. These credentials will also be used to upload metrics
     * to CloudWatch, unless {@link setMetricsCredentialsProvider} is used to
     * provide separate credentials for that.
     * 
     * @see #setCredentialsProvider(AWSCredentialsProvider)
     */
    public AWSCredentialsProvider getCredentialsProvider() {
        return credentialsProvider;
    }

    /**
     * {@link AWSCredentialsProvider} that supplies credentials used to put
     * records to Kinesis.
     * <p>
     * These credentials will also be used to upload metrics
     * to CloudWatch, unless {@link setMetricsCredentialsProvider} is used to
     * provide separate credentials for that.
     * <p>
     * Defaults to an instance of {@link DefaultAWSCredentialsProviderChain}
     * 
     * @see #setMetricsCredentialsProvider(AWSCredentialsProvider)
     */
    public KinesisProducerConfiguration setCredentialsProvider(AWSCredentialsProvider CredentialsProvider) {
        this.credentialsProvider = CredentialsProvider;
        return this;
    }

    /**
     * {@link AWSCredentialsProvider} that supplies credentials used to upload
     * metrics to CloudWatch. If not given, the credentials used to put records
     * to Kinesis are also used for CloudWatch.
     * 
     * @see #setMetricsCredentialsProvider(AWSCredentialsProvider)
     */
    public AWSCredentialsProvider getMetricsCredentialsProvider() {
        return metricsCredentialsProvider;
    }
    
    /**
     * {@link AWSCredentialsProvider} that supplies credentials used to upload
     * metrics to CloudWatch.
     * <p>
     * If not given, the credentials used to put records
     * to Kinesis are also used for CloudWatch.
     * 
     * @see #setCredentialsProvider(AWSCredentialsProvider)
     */
    public KinesisProducerConfiguration setMetricsCredentialsProvider(AWSCredentialsProvider metricsCredentialsProvider) {
        this.metricsCredentialsProvider = metricsCredentialsProvider;
        return this;
    }
    
    /**
     * Load configuration from a properties file. Any fields not found in the
     * target file will take on default values.
     *
     * <p>
     * The values loaded are checked against any constraints that each
     * respective field may have. If there are invalid values an
     * IllegalArgumentException will be thrown.
     *
     * @param path
     *            Path to the properties file containing KPL config.
     * @return A {@link KinesisProducerConfiguration} instance containing values
     *         loaded from the specified file.
     * @throws IllegalArgumentException
     *             If one or more config values are invalid.
     */
    public static KinesisProducerConfiguration fromPropertiesFile(String path) {
        log.info("Attempting to load config from file " + path);

        Properties props = new Properties();
        try (InputStream is = new FileInputStream(path)) {
            props.load(is);
        } catch (Exception e) {
            throw new RuntimeException("Error loading config from properties file", e);
        }

        return fromProperties(props);
    }

    /**
     * Load configuration from a {@link Properties} object. Any fields not found
     * in the properties instance will take on default values.
     *
     * <p>
     * The values loaded are checked against any constraints that each
     * respective field may have. If there are invalid values an
     * IllegalArgumentException will be thrown.
     *
     * @param props
     *            {@link Properties} object containing KPL config.
     * @return A {@link KinesisProducerConfiguration} instance containing values
     *         loaded from the specified file.
     * @throws IllegalArgumentException
     *             If one or more config values are invalid.
     */
    public static KinesisProducerConfiguration fromProperties(Properties props) {
        KinesisProducerConfiguration config = new KinesisProducerConfiguration();
        Enumeration<?> propNames = props.propertyNames();
        while (propNames.hasMoreElements()) {
            boolean found = false;
            String key = propNames.nextElement().toString();
            String value = props.getProperty(key);
            for (Method method : KinesisProducerConfiguration.class.getMethods()) {
                if (method.getName().equals("set" + key)) {
                    found = true;
                    Class<?> type = method.getParameterTypes()[0];
                    try {
                        if (type == long.class) {
                            method.invoke(config, Long.valueOf(value));
                        } else if (type == boolean.class) {
                            method.invoke(config, Boolean.valueOf(value));
                        } else if (type == String.class) {
                            method.invoke(config, value);
                        }
                    } catch (Exception e) {
                        throw new IllegalArgumentException(String.format(
                                "Error trying to set field %s with the value '%s'", "AggregationEnabled",
                                key, value), e);
                    }
                }
            }
            if (!found) {
                log.warn("Property " + key + " ignored as there is no corresponding set method in " +
                        KinesisProducerConfiguration.class.getSimpleName());
            }
        }
        
        return config;
    }
    
    protected Configuration.Builder additionalConfigsToProtobuf(Configuration.Builder builder) {
        return builder.addAllAdditionalMetricDims(additionalDims);
    }
    
    // __GENERATED_CODE__
    private boolean aggregationEnabled = true;
    private long aggregationMaxCount = 4294967295L;
    private long aggregationMaxSize = 51200L;
    private long collectionMaxCount = 500L;
    private long collectionMaxSize = 5242880L;
    private long connectTimeout = 6000L;
    private long credentialsRefreshDelay = 5000L;
    private String customEndpoint = "";
    private boolean failIfThrottled = false;
    private String logLevel = "info";
    private long maxConnections = 4L;
    private String metricsGranularity = "shard";
    private String metricsLevel = "detailed";
    private String metricsNamespace = "KinesisProducerLibrary";
    private long metricsUploadDelay = 60000L;
    private long minConnections = 1L;
    private String nativeExecutable = "";
    private long port = 443L;
    private long rateLimit = 150L;
    private long recordMaxBufferedTime = 100L;
    private long recordTtl = 30000L;
    private String region = "";
    private long requestTimeout = 6000L;
    private String tempDirectory = "";
    private boolean verifyCertificate = true;

    /**
     * Enable aggregation. With aggregation, multiple user records are packed into a single
     * KinesisRecord. If disabled, each user record is sent in its own KinesisRecord.
     * 
     * <p>
     * If your records are small, enabling aggregation will allow you to put many more records
     * than you would otherwise be able to for a shard before getting throttled.
     * 
     * <p><b>Default</b>: true
     */
    public boolean isAggregationEnabled() {
      return aggregationEnabled;
    }

    /**
     * Maximum number of items to pack into an aggregated record.
     * 
     * <p>
     * There should be normally no need to adjust this. If you want to limit the time records
     * spend buffering, look into record_max_buffered_time instead.
     * 
     * <p><b>Default</b>: 4294967295
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 9223372036854775807
     */
    public long getAggregationMaxCount() {
      return aggregationMaxCount;
    }

    /**
     * Maximum number of bytes to pack into an aggregated Kinesis record.
     * 
     * <p>
     * There should be normally no need to adjust this. If you want to limit the time records
     * spend buffering, look into record_max_buffered_time instead.
     * 
     * <p>
     * If a record has more data by itself than this limit, it will bypass the aggregator. Note
     * the backend enforces a limit of 50KB on record size. If you set this beyond 50KB, oversize
     * records will be rejected at the backend.
     * 
     * <p><b>Default</b>: 51200
     * <p><b>Minimum</b>: 64
     * <p><b>Maximum (inclusive)</b>: 1048576
     */
    public long getAggregationMaxSize() {
      return aggregationMaxSize;
    }

    /**
     * Maximum number of items to pack into an PutRecords request.
     * 
     * <p>
     * There should be normally no need to adjust this. If you want to limit the time records
     * spend buffering, look into record_max_buffered_time instead.
     * 
     * <p><b>Default</b>: 500
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 500
     */
    public long getCollectionMaxCount() {
      return collectionMaxCount;
    }

    /**
     * Maximum amount of data to send with a PutRecords request.
     * 
     * <p>
     * There should be normally no need to adjust this. If you want to limit the time records
     * spend buffering, look into record_max_buffered_time instead.
     * 
     * <p>
     * Records larger than the limit will still be sent, but will not be grouped with others.
     * 
     * <p><b>Default</b>: 5242880
     * <p><b>Minimum</b>: 52224
     * <p><b>Maximum (inclusive)</b>: 9223372036854775807
     */
    public long getCollectionMaxSize() {
      return collectionMaxSize;
    }

    /**
     * Timeout (milliseconds) for establishing TLS connections.
     * 
     * <p><b>Default</b>: 6000
     * <p><b>Minimum</b>: 100
     * <p><b>Maximum (inclusive)</b>: 300000
     */
    public long getConnectTimeout() {
      return connectTimeout;
    }

    /**
     * How often to refresh credentials (in milliseconds).
     * 
     * <p>
     * During a refresh, credentials are retrieved from any SDK credentials providers attached to
     * the wrapper and pushed to the core.
     * 
     * <p><b>Default</b>: 5000
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 300000
     */
    public long getCredentialsRefreshDelay() {
      return credentialsRefreshDelay;
    }

    /**
     * Use a custom Kinesis and CloudWatch endpoint.
     * 
     * <p>
     * Mostly for testing use. Note this does not accept protocols or paths, only host names or ip
     * addresses. There is no way to disable TLS. The KPL always connects with TLS.
     * 
     * <p><b>Expected pattern</b>: ^([A-Za-z0-9-\\.]+)?$
     */
    public String getCustomEndpoint() {
      return customEndpoint;
    }

    /**
     * If true, throttled puts are not retried. The records that got throttled will be failed
     * immediately upon receiving the throttling error. This is useful if you want to react
     * immediately to any throttling without waiting for the KPL to retry. For example, you can
     * use a different hash key to send the throttled record to a backup shard.
     * 
     * <p>
     * If false, the KPL will automatically retry throttled puts. The KPL performs backoff for
     * shards that it has received throttling errors from, and will avoid flooding them with
     * retries. Note that records may fail from expiration (see record_ttl) if they get delayed
     * for too long because of throttling.
     * 
     * <p><b>Default</b>: false
     */
    public boolean isFailIfThrottled() {
      return failIfThrottled;
    }

    /**
     * Minimum level of logs. Messages below the specified level will not be logged. Logs for the
     * native KPL daemon show up on stderr.
     * 
     * <p><b>Default</b>: info
     * <p><b>Expected pattern</b>: info|warning|error
     */
    public String getLogLevel() {
      return logLevel;
    }

    /**
     * Maximum number of connections to open to the backend. HTTP requests are sent in parallel
     * over multiple connections.
     * 
     * <p>
     * Setting this too high may impact latency and consume additional resources without
     * increasing throughput.
     * 
     * <p><b>Default</b>: 4
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 128
     */
    public long getMaxConnections() {
      return maxConnections;
    }

    /**
     * Controls the granularity of metrics that are uploaded to CloudWatch. Greater granularity
     * produces more metrics.
     * 
     * <p>
     * When "shard" is selected, metrics are emitted with the stream name and shard id as
     * dimensions. On top of this, the same metric is also emitted with only the stream name
     * dimension, and lastly, without the stream name. This means for a particular metric, 2
     * streams with 2 shards (each) will produce 7 CloudWatch metrics, one for each shard, one for
     * each stream, and one overall, all describing the same statistics, but at different levels
     * of granularity.
     * 
     * <p>
     * When "stream" is selected, per shard metrics are not uploaded; when "global" is selected,
     * only the total aggregate for all streams and all shards are uploaded.
     * 
     * <p>
     * Consider reducing the granularity if you're not interested in shard-level metrics, or if
     * you have a large number of shards.
     * 
     * <p>
     * If you only have 1 stream, select "global"; the global data will be equivalent to that for
     * the stream.
     * 
     * <p>
     * Refer to the metrics documentation for details about each metric.
     * 
     * <p><b>Default</b>: shard
     * <p><b>Expected pattern</b>: global|stream|shard
     */
    public String getMetricsGranularity() {
      return metricsGranularity;
    }

    /**
     * Controls the number of metrics that are uploaded to CloudWatch.
     * 
     * <p>
     * "none" disables all metrics.
     * 
     * <p>
     * "summary" enables the following metrics: UserRecordsPut, KinesisRecordsPut, ErrorsByCode,
     * AllErrors, BufferingTime.
     * 
     * <p>
     * "detailed" enables all remaining metrics.
     * 
     * <p>
     * Refer to the metrics documentation for details about each metric.
     * 
     * <p><b>Default</b>: detailed
     * <p><b>Expected pattern</b>: none|summary|detailed
     */
    public String getMetricsLevel() {
      return metricsLevel;
    }

    /**
     * The namespace to upload metrics under.
     * 
     * <p>
     * If you have multiple applications running the KPL under the same AWS account, you should
     * use a different namespace for each application.
     * 
     * <p>
     * If you are also using the KCL, you may wish to use the application name you have configured
     * for the KCL as the the namespace here. This way both your KPL and KCL metrics show up under
     * the same namespace.
     * 
     * <p><b>Default</b>: KinesisProducerLibrary
     * <p><b>Expected pattern</b>: (?!AWS/).{1,255}
     */
    public String getMetricsNamespace() {
      return metricsNamespace;
    }

    /**
     * Delay (in milliseconds) between each metrics upload.
     * 
     * <p>
     * For testing only. There is no benefit in setting this lower or higher in production.
     * 
     * <p><b>Default</b>: 60000
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 60000
     */
    public long getMetricsUploadDelay() {
      return metricsUploadDelay;
    }

    /**
     * Minimum number of connections to keep open to the backend.
     * 
     * <p>
     * There should be no need to increase this in general.
     * 
     * <p><b>Default</b>: 1
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 16
     */
    public long getMinConnections() {
      return minConnections;
    }

    /**
     * Path to the native KPL binary. Only use this setting if you want to use a custom build of
     * the native code.
     * 
     */
    public String getNativeExecutable() {
      return nativeExecutable;
    }

    /**
     * Server port to connect to. Only useful with custom_endpoint.
     * 
     * <p><b>Default</b>: 443
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 65535
     */
    public long getPort() {
      return port;
    }

    /**
     * Limits the maximum allowed put rate for a shard, as a percentage of the backend limits.
     * 
     * <p>
     * The rate limit prevents the producer from sending data too fast to a shard. Such a limit is
     * useful for reducing bandwidth and CPU cycle wastage from sending requests that we know are
     * going to fail from throttling.
     * 
     * <p>
     * Kinesis enforces limits on both the number of records and number of bytes per second. This
     * setting applies to both.
     * 
     * <p>
     * The default value of 150% is chosen to allow a single producer instance to completely
     * saturate the allowance for a shard. This is an aggressive setting. If you prefer to reduce
     * throttling errors rather than completely saturate the shard, consider reducing this
     * setting.
     * 
     * <p><b>Default</b>: 150
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 9223372036854775807
     */
    public long getRateLimit() {
      return rateLimit;
    }

    /**
     * Maximum amount of itme (milliseconds) a record may spend being buffered before it gets
     * sent. Records may be sent sooner than this depending on the other buffering limits.
     * 
     * <p>
     * This setting provides coarse ordering among records - any two records will be reordered by
     * no more than twice this amount (assuming no failures and retries and equal network
     * latency).
     * 
     * <p>
     * The library makes a best effort to enforce this time, but cannot guarantee that it will be
     * precisely met. In general, if the CPU is not overloaded, the library will meet this
     * deadline to within 10ms.
     * 
     * <p>
     * Failures and retries can additionally increase the amount of time records spend in the KPL.
     * If your application cannot tolerate late records, use the record_ttl setting to drop
     * records that do not get transmitted in time.
     * 
     * <p>
     * Setting this too low can negatively impact throughput.
     * 
     * <p><b>Default</b>: 100
     * <p><b>Maximum (inclusive)</b>: 9223372036854775807
     */
    public long getRecordMaxBufferedTime() {
      return recordMaxBufferedTime;
    }

    /**
     * Set a time-to-live on records (milliseconds). Records that do not get successfully put
     * within the limit are failed.
     * 
     * <p>
     * This setting is useful if your application cannot or does not wish to tolerate late
     * records. Records will still incur network latency after they leave the KPL, so take that
     * into consideration when choosing a value for this setting.
     * 
     * <p>
     * If you do not wish to lose records and prefer to retry indefinitely, set record_ttl to a
     * large value like INT_MAX. This has the potential to cause head-of-line blocking if network
     * issues or throttling occur. You can respond to such situations by using the metrics
     * reporting functions of the KPL. You may also set fail_if_throttled to true to prevent
     * automatic retries in case of throttling.
     * 
     * <p><b>Default</b>: 30000
     * <p><b>Minimum</b>: 100
     * <p><b>Maximum (inclusive)</b>: 9223372036854775807
     */
    public long getRecordTtl() {
      return recordTtl;
    }

    /**
     * Which region to send records to.
     * 
     * <p>
     * If you do not specify the region and are running in EC2, the library will use the region
     * the instance is in.
     * 
     * <p>
     * The region is also used to sign requests.
     * 
     * <p><b>Expected pattern</b>: ^([a-z]+-[a-z]+-[0-9])?$
     */
    public String getRegion() {
      return region;
    }

    /**
     * The maximum total time (milliseconds) elapsed between when we begin a HTTP request and
     * receiving all of the response. If it goes over, the request will be timed-out.
     * 
     * <p>
     * Note that a timed-out request may actually succeed at the backend. Retrying then leads to
     * duplicates. Setting the timeout too low will therefore increase the probability of
     * duplicates.
     * 
     * <p><b>Default</b>: 6000
     * <p><b>Minimum</b>: 100
     * <p><b>Maximum (inclusive)</b>: 600000
     */
    public long getRequestTimeout() {
      return requestTimeout;
    }

    /**
     * Temp directory into which to extract the native binaries. The KPL requires write
     * permissions in this directory.
     * 
     * <p>
     * If not specified, defaults to /tmp in Unix. (Windows TBD)
     * 
     */
    public String getTempDirectory() {
      return tempDirectory;
    }

    /**
     * Verify the endpoint's certificate. Do not disable unless using custom_endpoint for testing.
     * Never disable this in production.
     * 
     * <p><b>Default</b>: true
     */
    public boolean isVerifyCertificate() {
      return verifyCertificate;
    }

    /**
     * Enable aggregation. With aggregation, multiple user records are packed into a single
     * KinesisRecord. If disabled, each user record is sent in its own KinesisRecord.
     * 
     * <p>
     * If your records are small, enabling aggregation will allow you to put many more records
     * than you would otherwise be able to for a shard before getting throttled.
     * 
     * <p><b>Default</b>: true
     */
    public KinesisProducerConfiguration setAggregationEnabled(boolean val) {
        aggregationEnabled = val;
        return this;
    }

    /**
     * Maximum number of items to pack into an aggregated record.
     * 
     * <p>
     * There should be normally no need to adjust this. If you want to limit the time records
     * spend buffering, look into record_max_buffered_time instead.
     * 
     * <p><b>Default</b>: 4294967295
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 9223372036854775807
     */
    public KinesisProducerConfiguration setAggregationMaxCount(long val) {
        if (val < 1L || val > 9223372036854775807L) {
            throw new IllegalArgumentException("aggregationMaxCount must be between 1 and 9223372036854775807, got " + val);
        }
        aggregationMaxCount = val;
        return this;
    }

    /**
     * Maximum number of bytes to pack into an aggregated Kinesis record.
     * 
     * <p>
     * There should be normally no need to adjust this. If you want to limit the time records
     * spend buffering, look into record_max_buffered_time instead.
     * 
     * <p>
     * If a record has more data by itself than this limit, it will bypass the aggregator. Note
     * the backend enforces a limit of 50KB on record size. If you set this beyond 50KB, oversize
     * records will be rejected at the backend.
     * 
     * <p><b>Default</b>: 51200
     * <p><b>Minimum</b>: 64
     * <p><b>Maximum (inclusive)</b>: 1048576
     */
    public KinesisProducerConfiguration setAggregationMaxSize(long val) {
        if (val < 64L || val > 1048576L) {
            throw new IllegalArgumentException("aggregationMaxSize must be between 64 and 1048576, got " + val);
        }
        aggregationMaxSize = val;
        return this;
    }

    /**
     * Maximum number of items to pack into an PutRecords request.
     * 
     * <p>
     * There should be normally no need to adjust this. If you want to limit the time records
     * spend buffering, look into record_max_buffered_time instead.
     * 
     * <p><b>Default</b>: 500
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 500
     */
    public KinesisProducerConfiguration setCollectionMaxCount(long val) {
        if (val < 1L || val > 500L) {
            throw new IllegalArgumentException("collectionMaxCount must be between 1 and 500, got " + val);
        }
        collectionMaxCount = val;
        return this;
    }

    /**
     * Maximum amount of data to send with a PutRecords request.
     * 
     * <p>
     * There should be normally no need to adjust this. If you want to limit the time records
     * spend buffering, look into record_max_buffered_time instead.
     * 
     * <p>
     * Records larger than the limit will still be sent, but will not be grouped with others.
     * 
     * <p><b>Default</b>: 5242880
     * <p><b>Minimum</b>: 52224
     * <p><b>Maximum (inclusive)</b>: 9223372036854775807
     */
    public KinesisProducerConfiguration setCollectionMaxSize(long val) {
        if (val < 52224L || val > 9223372036854775807L) {
            throw new IllegalArgumentException("collectionMaxSize must be between 52224 and 9223372036854775807, got " + val);
        }
        collectionMaxSize = val;
        return this;
    }

    /**
     * Timeout (milliseconds) for establishing TLS connections.
     * 
     * <p><b>Default</b>: 6000
     * <p><b>Minimum</b>: 100
     * <p><b>Maximum (inclusive)</b>: 300000
     */
    public KinesisProducerConfiguration setConnectTimeout(long val) {
        if (val < 100L || val > 300000L) {
            throw new IllegalArgumentException("connectTimeout must be between 100 and 300000, got " + val);
        }
        connectTimeout = val;
        return this;
    }

    /**
     * How often to refresh credentials (in milliseconds).
     * 
     * <p>
     * During a refresh, credentials are retrieved from any SDK credentials providers attached to
     * the wrapper and pushed to the core.
     * 
     * <p><b>Default</b>: 5000
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 300000
     */
    public KinesisProducerConfiguration setCredentialsRefreshDelay(long val) {
        if (val < 1L || val > 300000L) {
            throw new IllegalArgumentException("credentialsRefreshDelay must be between 1 and 300000, got " + val);
        }
        credentialsRefreshDelay = val;
        return this;
    }

    /**
     * Use a custom Kinesis and CloudWatch endpoint.
     * 
     * <p>
     * Mostly for testing use. Note this does not accept protocols or paths, only host names or ip
     * addresses. There is no way to disable TLS. The KPL always connects with TLS.
     * 
     * <p><b>Expected pattern</b>: ^([A-Za-z0-9-\\.]+)?$
     */
    public KinesisProducerConfiguration setCustomEndpoint(String val) {
        if (!Pattern.matches("^([A-Za-z0-9-\\.]+)?$", val)) {
            throw new IllegalArgumentException("customEndpoint must match the pattern ^([A-Za-z0-9-\\.]+)?$, got " + val);
        }
        customEndpoint = val;
        return this;
    }

    /**
     * If true, throttled puts are not retried. The records that got throttled will be failed
     * immediately upon receiving the throttling error. This is useful if you want to react
     * immediately to any throttling without waiting for the KPL to retry. For example, you can
     * use a different hash key to send the throttled record to a backup shard.
     * 
     * <p>
     * If false, the KPL will automatically retry throttled puts. The KPL performs backoff for
     * shards that it has received throttling errors from, and will avoid flooding them with
     * retries. Note that records may fail from expiration (see record_ttl) if they get delayed
     * for too long because of throttling.
     * 
     * <p><b>Default</b>: false
     */
    public KinesisProducerConfiguration setFailIfThrottled(boolean val) {
        failIfThrottled = val;
        return this;
    }

    /**
     * Minimum level of logs. Messages below the specified level will not be logged. Logs for the
     * native KPL daemon show up on stderr.
     * 
     * <p><b>Default</b>: info
     * <p><b>Expected pattern</b>: info|warning|error
     */
    public KinesisProducerConfiguration setLogLevel(String val) {
        if (!Pattern.matches("info|warning|error", val)) {
            throw new IllegalArgumentException("logLevel must match the pattern info|warning|error, got " + val);
        }
        logLevel = val;
        return this;
    }

    /**
     * Maximum number of connections to open to the backend. HTTP requests are sent in parallel
     * over multiple connections.
     * 
     * <p>
     * Setting this too high may impact latency and consume additional resources without
     * increasing throughput.
     * 
     * <p><b>Default</b>: 4
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 128
     */
    public KinesisProducerConfiguration setMaxConnections(long val) {
        if (val < 1L || val > 128L) {
            throw new IllegalArgumentException("maxConnections must be between 1 and 128, got " + val);
        }
        maxConnections = val;
        return this;
    }

    /**
     * Controls the granularity of metrics that are uploaded to CloudWatch. Greater granularity
     * produces more metrics.
     * 
     * <p>
     * When "shard" is selected, metrics are emitted with the stream name and shard id as
     * dimensions. On top of this, the same metric is also emitted with only the stream name
     * dimension, and lastly, without the stream name. This means for a particular metric, 2
     * streams with 2 shards (each) will produce 7 CloudWatch metrics, one for each shard, one for
     * each stream, and one overall, all describing the same statistics, but at different levels
     * of granularity.
     * 
     * <p>
     * When "stream" is selected, per shard metrics are not uploaded; when "global" is selected,
     * only the total aggregate for all streams and all shards are uploaded.
     * 
     * <p>
     * Consider reducing the granularity if you're not interested in shard-level metrics, or if
     * you have a large number of shards.
     * 
     * <p>
     * If you only have 1 stream, select "global"; the global data will be equivalent to that for
     * the stream.
     * 
     * <p>
     * Refer to the metrics documentation for details about each metric.
     * 
     * <p><b>Default</b>: shard
     * <p><b>Expected pattern</b>: global|stream|shard
     */
    public KinesisProducerConfiguration setMetricsGranularity(String val) {
        if (!Pattern.matches("global|stream|shard", val)) {
            throw new IllegalArgumentException("metricsGranularity must match the pattern global|stream|shard, got " + val);
        }
        metricsGranularity = val;
        return this;
    }

    /**
     * Controls the number of metrics that are uploaded to CloudWatch.
     * 
     * <p>
     * "none" disables all metrics.
     * 
     * <p>
     * "summary" enables the following metrics: UserRecordsPut, KinesisRecordsPut, ErrorsByCode,
     * AllErrors, BufferingTime.
     * 
     * <p>
     * "detailed" enables all remaining metrics.
     * 
     * <p>
     * Refer to the metrics documentation for details about each metric.
     * 
     * <p><b>Default</b>: detailed
     * <p><b>Expected pattern</b>: none|summary|detailed
     */
    public KinesisProducerConfiguration setMetricsLevel(String val) {
        if (!Pattern.matches("none|summary|detailed", val)) {
            throw new IllegalArgumentException("metricsLevel must match the pattern none|summary|detailed, got " + val);
        }
        metricsLevel = val;
        return this;
    }

    /**
     * The namespace to upload metrics under.
     * 
     * <p>
     * If you have multiple applications running the KPL under the same AWS account, you should
     * use a different namespace for each application.
     * 
     * <p>
     * If you are also using the KCL, you may wish to use the application name you have configured
     * for the KCL as the the namespace here. This way both your KPL and KCL metrics show up under
     * the same namespace.
     * 
     * <p><b>Default</b>: KinesisProducerLibrary
     * <p><b>Expected pattern</b>: (?!AWS/).{1,255}
     */
    public KinesisProducerConfiguration setMetricsNamespace(String val) {
        if (!Pattern.matches("(?!AWS/).{1,255}", val)) {
            throw new IllegalArgumentException("metricsNamespace must match the pattern (?!AWS/).{1,255}, got " + val);
        }
        metricsNamespace = val;
        return this;
    }

    /**
     * Delay (in milliseconds) between each metrics upload.
     * 
     * <p>
     * For testing only. There is no benefit in setting this lower or higher in production.
     * 
     * <p><b>Default</b>: 60000
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 60000
     */
    public KinesisProducerConfiguration setMetricsUploadDelay(long val) {
        if (val < 1L || val > 60000L) {
            throw new IllegalArgumentException("metricsUploadDelay must be between 1 and 60000, got " + val);
        }
        metricsUploadDelay = val;
        return this;
    }

    /**
     * Minimum number of connections to keep open to the backend.
     * 
     * <p>
     * There should be no need to increase this in general.
     * 
     * <p><b>Default</b>: 1
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 16
     */
    public KinesisProducerConfiguration setMinConnections(long val) {
        if (val < 1L || val > 16L) {
            throw new IllegalArgumentException("minConnections must be between 1 and 16, got " + val);
        }
        minConnections = val;
        return this;
    }

    /**
     * Path to the native KPL binary. Only use this setting if you want to use a custom build of
     * the native code.
     * 
     */
    public KinesisProducerConfiguration setNativeExecutable(String val) {
        nativeExecutable = val;
        return this;
    }

    /**
     * Server port to connect to. Only useful with custom_endpoint.
     * 
     * <p><b>Default</b>: 443
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 65535
     */
    public KinesisProducerConfiguration setPort(long val) {
        if (val < 1L || val > 65535L) {
            throw new IllegalArgumentException("port must be between 1 and 65535, got " + val);
        }
        port = val;
        return this;
    }

    /**
     * Limits the maximum allowed put rate for a shard, as a percentage of the backend limits.
     * 
     * <p>
     * The rate limit prevents the producer from sending data too fast to a shard. Such a limit is
     * useful for reducing bandwidth and CPU cycle wastage from sending requests that we know are
     * going to fail from throttling.
     * 
     * <p>
     * Kinesis enforces limits on both the number of records and number of bytes per second. This
     * setting applies to both.
     * 
     * <p>
     * The default value of 150% is chosen to allow a single producer instance to completely
     * saturate the allowance for a shard. This is an aggressive setting. If you prefer to reduce
     * throttling errors rather than completely saturate the shard, consider reducing this
     * setting.
     * 
     * <p><b>Default</b>: 150
     * <p><b>Minimum</b>: 1
     * <p><b>Maximum (inclusive)</b>: 9223372036854775807
     */
    public KinesisProducerConfiguration setRateLimit(long val) {
        if (val < 1L || val > 9223372036854775807L) {
            throw new IllegalArgumentException("rateLimit must be between 1 and 9223372036854775807, got " + val);
        }
        rateLimit = val;
        return this;
    }

    /**
     * Maximum amount of itme (milliseconds) a record may spend being buffered before it gets
     * sent. Records may be sent sooner than this depending on the other buffering limits.
     * 
     * <p>
     * This setting provides coarse ordering among records - any two records will be reordered by
     * no more than twice this amount (assuming no failures and retries and equal network
     * latency).
     * 
     * <p>
     * The library makes a best effort to enforce this time, but cannot guarantee that it will be
     * precisely met. In general, if the CPU is not overloaded, the library will meet this
     * deadline to within 10ms.
     * 
     * <p>
     * Failures and retries can additionally increase the amount of time records spend in the KPL.
     * If your application cannot tolerate late records, use the record_ttl setting to drop
     * records that do not get transmitted in time.
     * 
     * <p>
     * Setting this too low can negatively impact throughput.
     * 
     * <p><b>Default</b>: 100
     * <p><b>Maximum (inclusive)</b>: 9223372036854775807
     */
    public KinesisProducerConfiguration setRecordMaxBufferedTime(long val) {
        if (val < 0L || val > 9223372036854775807L) {
            throw new IllegalArgumentException("recordMaxBufferedTime must be between 0 and 9223372036854775807, got " + val);
        }
        recordMaxBufferedTime = val;
        return this;
    }

    /**
     * Set a time-to-live on records (milliseconds). Records that do not get successfully put
     * within the limit are failed.
     * 
     * <p>
     * This setting is useful if your application cannot or does not wish to tolerate late
     * records. Records will still incur network latency after they leave the KPL, so take that
     * into consideration when choosing a value for this setting.
     * 
     * <p>
     * If you do not wish to lose records and prefer to retry indefinitely, set record_ttl to a
     * large value like INT_MAX. This has the potential to cause head-of-line blocking if network
     * issues or throttling occur. You can respond to such situations by using the metrics
     * reporting functions of the KPL. You may also set fail_if_throttled to true to prevent
     * automatic retries in case of throttling.
     * 
     * <p><b>Default</b>: 30000
     * <p><b>Minimum</b>: 100
     * <p><b>Maximum (inclusive)</b>: 9223372036854775807
     */
    public KinesisProducerConfiguration setRecordTtl(long val) {
        if (val < 100L || val > 9223372036854775807L) {
            throw new IllegalArgumentException("recordTtl must be between 100 and 9223372036854775807, got " + val);
        }
        recordTtl = val;
        return this;
    }

    /**
     * Which region to send records to.
     * 
     * <p>
     * If you do not specify the region and are running in EC2, the library will use the region
     * the instance is in.
     * 
     * <p>
     * The region is also used to sign requests.
     * 
     * <p><b>Expected pattern</b>: ^([a-z]+-[a-z]+-[0-9])?$
     */
    public KinesisProducerConfiguration setRegion(String val) {
        if (!Pattern.matches("^([a-z]+-[a-z]+-[0-9])?$", val)) {
            throw new IllegalArgumentException("region must match the pattern ^([a-z]+-[a-z]+-[0-9])?$, got " + val);
        }
        region = val;
        return this;
    }

    /**
     * The maximum total time (milliseconds) elapsed between when we begin a HTTP request and
     * receiving all of the response. If it goes over, the request will be timed-out.
     * 
     * <p>
     * Note that a timed-out request may actually succeed at the backend. Retrying then leads to
     * duplicates. Setting the timeout too low will therefore increase the probability of
     * duplicates.
     * 
     * <p><b>Default</b>: 6000
     * <p><b>Minimum</b>: 100
     * <p><b>Maximum (inclusive)</b>: 600000
     */
    public KinesisProducerConfiguration setRequestTimeout(long val) {
        if (val < 100L || val > 600000L) {
            throw new IllegalArgumentException("requestTimeout must be between 100 and 600000, got " + val);
        }
        requestTimeout = val;
        return this;
    }

    /**
     * Temp directory into which to extract the native binaries. The KPL requires write
     * permissions in this directory.
     * 
     * <p>
     * If not specified, defaults to /tmp in Unix. (Windows TBD)
     * 
     */
    public KinesisProducerConfiguration setTempDirectory(String val) {
        tempDirectory = val;
        return this;
    }

    /**
     * Verify the endpoint's certificate. Do not disable unless using custom_endpoint for testing.
     * Never disable this in production.
     * 
     * <p><b>Default</b>: true
     */
    public KinesisProducerConfiguration setVerifyCertificate(boolean val) {
        verifyCertificate = val;
        return this;
    }


    protected Message toProtobufMessage() {
        Configuration c = this.additionalConfigsToProtobuf(
            Configuration.newBuilder()
                .setAggregationEnabled(aggregationEnabled)
                .setAggregationMaxCount(aggregationMaxCount)
                .setAggregationMaxSize(aggregationMaxSize)
                .setCollectionMaxCount(collectionMaxCount)
                .setCollectionMaxSize(collectionMaxSize)
                .setConnectTimeout(connectTimeout)
                .setCustomEndpoint(customEndpoint)
                .setFailIfThrottled(failIfThrottled)
                .setLogLevel(logLevel)
                .setMaxConnections(maxConnections)
                .setMetricsGranularity(metricsGranularity)
                .setMetricsLevel(metricsLevel)
                .setMetricsNamespace(metricsNamespace)
                .setMetricsUploadDelay(metricsUploadDelay)
                .setMinConnections(minConnections)
                .setPort(port)
                .setRateLimit(rateLimit)
                .setRecordMaxBufferedTime(recordMaxBufferedTime)
                .setRecordTtl(recordTtl)
                .setRegion(region)
                .setRequestTimeout(requestTimeout)
                .setVerifyCertificate(verifyCertificate)
                ).build();
       return Message.newBuilder()
                      .setConfiguration(c)
                      .setId(0)
                      .build();
    }
}
