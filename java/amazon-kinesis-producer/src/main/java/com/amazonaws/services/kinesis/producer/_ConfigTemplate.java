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
@SuppressWarnings("unused")
class _ConfigTemplate {
    private static final Logger log = LoggerFactory.getLogger(_ConfigTemplate.class);

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
    public _ConfigTemplate setCredentialsProvider(AWSCredentialsProvider CredentialsProvider) {
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
    public _ConfigTemplate setMetricsCredentialsProvider(AWSCredentialsProvider metricsCredentialsProvider) {
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
    public static _ConfigTemplate fromPropertiesFile(String path) {
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
    public static _ConfigTemplate fromProperties(Properties props) {
        _ConfigTemplate config = new _ConfigTemplate();
        Enumeration<?> propNames = props.propertyNames();
        while (propNames.hasMoreElements()) {
            boolean found = false;
            String key = propNames.nextElement().toString();
            String value = props.getProperty(key);
            for (Method method : _ConfigTemplate.class.getMethods()) {
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
                        _ConfigTemplate.class.getSimpleName());
            }
        }
        
        return config;
    }
    
    protected Configuration.Builder additionalConfigsToProtobuf(Configuration.Builder builder) {
        return builder.addAllAdditionalMetricDims(additionalDims);
    }
    
    // __GENERATED_CODE__
}
