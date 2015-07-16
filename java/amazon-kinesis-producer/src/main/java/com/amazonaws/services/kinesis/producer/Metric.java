/*
 * Copyright 2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 * http://aws.amazon.com/asl/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

package com.amazonaws.services.kinesis.producer;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

import com.amazonaws.services.kinesis.producer.protobuf.Messages.Dimension;
import com.amazonaws.services.kinesis.producer.protobuf.Messages.Stats;

/**
 * A metric consists of a name, a list of dimensions, a set of statistics, and
 * the duration over which the statistics were collected.
 * 
 * <p>
 * There are typically many Metric instances for each metric name. Each one will
 * have a different list of dimensions.
 * 
 * <p>
 * This class is immutable.
 * 
 * @author chaodeng
 *
 */
public class Metric {
    private final String name;
    private long duration;
    private final Map<String, String> dimensions;
    private final double sum;
    private final double mean;
    private final double sampleCount;
    private final double min;
    private final double max;
    
    /**
     * Gets the dimensions of this metric. The returned map has appropriate
     * iteration order and is immutable.
     * 
     * @return Immutable map containing the dimensions.
     */
    public Map<String, String> getDimensions() {
        return Collections.unmodifiableMap(dimensions);
    }
    
    public double getSum() {
        return sum;
    }

    public double getMean() {
        return mean;
    }

    public double getSampleCount() {
        return sampleCount;
    }

    public double getMin() {
        return min;
    }

    public double getMax() {
        return max;
    }
    
    public String getName() {
        return name;
    }

    /**
     * @return The number of seconds over which the statistics in this Metric
     *         instance was accumulated. For example, a duration of 10 means
     *         that the statistics in this Metric instance represents 10 seconds
     *         worth of samples.
     */
    public long getDuration() {
        return duration;
    }

    protected Metric(com.amazonaws.services.kinesis.producer.protobuf.Messages.Metric m) {
        this.name = m.getName();
        this.duration = m.getSeconds();
        
        dimensions = new LinkedHashMap<String, String>();
        for (Dimension d : m.getDimensionsList()) {
            dimensions.put(d.getKey(), d.getValue());
        }
        
        Stats s = m.getStats();
        this.max = s.getMax();
        this.mean = s.getMean();
        this.min = s.getMin();
        this.sum = s.getSum();
        this.sampleCount = s.getCount();
    }

    @Override
    public String toString() {
        return "Metric [name=" + name + ", duration=" + duration + ", dimensions=" + dimensions + ", sum=" + sum
                + ", mean=" + mean + ", sampleCount=" + sampleCount + ", min=" + min + ", max=" + max + "]";
    }
}
