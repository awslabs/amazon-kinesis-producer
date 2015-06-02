# KPL Metrics

## Intro

The KPL tracks a number of metrics that may be useful to customers.

## Concepts

### Metrics, Dimension, Namespaces, etc.

We define these concepts exactly the [same way Amazon CloudWatch does](http://docs.aws.amazon.com/AmazonCloudWatch/latest/DeveloperGuide/cloudwatch_concepts.html). This is because we intend to upload KPL metrics to CloudWatch.

Customers can specify an application name when launching the KPL, which we will then use as part of the namespace when uploading metrics. This is optional; we use a default value if app name is not set.

Users can additionally configure the KPL to add arbitrary additional dimensions to the metrics. This is useful if customers want finer grained data in their CloudWatch metrics. For example, they can add the host name as a dimension, which will then allow them to identify uneven load distribution across their fleet. All KPL configuration settings are immutable at the moment, so these additional dimensions cannot be easily changed.

### Metric Level & Granularity

Customers may be interested in having some metrics in CloudWatch but not others, for reasons of cost. As such, we provide two options to control the number of metrics uploaded to CloudWatch.

The _metric level_ is a rough gauge of how important a metric is. Every metric is assigned a level. When customers set a level, metrics with levels below that are not sent to CloudWatch. The levels are NONE, SUMMARY, and DETAILED. The default setting is DETAILED; that is, all metrics. NONE means no metrics at all, so no metrics are actually assigned to that level.

The _granularity_ controls whether the same metric is emitted at additional levels of granularity. The levels are GLOBAL, STREAM, and SHARD. The default setting is SHARD, which contains the most granular metrics. 

When SHARD is chosen, metrics are emitted with the stream name and shard ID as dimensions. In addition, the same metric is also emitted with only the stream name dimension, and lastly, without the stream name. This means for a particular metric, two streams with two shards each will produce seven CloudWatch metrics, one for each shard, one for each stream, and one overall, all describing the same statistics but at different levels of granularity. For more information, see the diagram below.

The different granularity levels form a hierarchy, and all the metrics in the system form  trees, rooted at the metric names:

```
MetricName (GLOBAL):           Metric X                    Metric Y
                                  |                           |
                           -----------------             ------------
                           |               |             |          |
StreamName (STREAM):    Stream A        Stream B      Stream A   Stream B
                           |               |
                        --------        ---------
                        |      |        |       |
ShardId (SHARD):     Shard 0 Shard 1  Shard 0 Shard 1
```

Not all metrics are available at the shard level; some are stream level or global by nature. These will not be produced at shard level even if the user has enabled shard-level metrics (Metric Y in diagram above).

When users specify an additional dimension, they will provide a tuple: `<DimensionName, DimensionValue, Granularity>`. The granularity is used to determine where the custom dimension is inserted in the hierarchy. GLOBAL means the additional dimension is inserted after the metric name, STREAM means it's inserted after StreamName, and SHARD means it's inserted after ShardId. If multiple additional dimensions are given per granularity level, they are inserted in the order they were given.

## Local Access

Metrics for the current KPL instance are available locally in real time. The user can query the KPL at any time to get them.

### Available Statistics

We compute the sum, average, min, max, and count of every metric, just like CloudWatch. Percentiles are not currently supported by CloudWatch, or by Amazon Kinesis (until CloudWatch adds support).

The user can get statistics that are cumulative from the start of the program to the present point in time, or just over the past N seconds, where N is an integer between 1 and 60 (that is, a rolling window).

## CloudWatch Upload

All metrics are available for upload to CloudWatch. This is useful for aggregating data across multiple hosts, which is not available locally, as well as monitoring and alarming. 

As described earlier, users can select which metrics to upload with the _metric level_ and _granularity_ settings. Metrics that are not uploaded are still available locally.

### Aggregation

Uploading data points individually is untenable because we can potentially produce millions of them per second if traffic is high.

As such, we aggregate metrics locally into 1 minute buckets and upload a statistics object to CloudWatch once per minute per (enabled) metric.

## List of Metrics

#### User Record Received

Metric Level: Detailed

Unit: Count

Count of how many logical user records were received by the KPL core for putting.

Not available at shard level.

-----

#### User Records Pending

Metric Level: Detailed

Unit: Count

Periodic sample of how many user records are currently pending. A record is pending if it is either currently buffered and waiting for to be sent, or sent and in-flight to the backend.

Not available at shard level.

The wrapper provides a dedicated method to retrieve this metric at the global level for customers to manage their put rate.

-----

#### User Records Put

Metric Level: Summary

Unit: Count

Count of how many logical user records were put successfully.

The KPL outputs a zero for failed records. This allows the average to give the success rate, the count to give the total attempts, and the difference between the count and sum to give the failure count.

-----

#### User Records Data Put

Metric Level: Detailed

Unit: Bytes

Bytes in the logical user records successfully put.

-----

#### Kinesis Records Put

Metric Level: Summary

Unit: Count

Count of how many Amazon Kinesis records were put successfully. Recall that each Amazon Kinesis record can contain multiple user records.

The KPL outputs a zero for failed records. This allows the average to give the success rate, the count to give the total attempts, and the difference between the count and sum to give the failure count.

-----

#### Kinesis Records Data Put

Metric Level: Detailed

Unit: Bytes

Bytes in the Amazon Kinesis records.

----

#### Errors by Code

Metric Level: Summary

Unit: Count

Count of each type of error code. This introduces an additional dimension of ErrorCode, on top of the normal ones like StreamName and ShardId.

Not every error can be traced to a shard. The ones that cannot be will only be emitted at stream or global levels.

This captures: throttling, shard map changes, internal failures, service unavailable, timeouts, etc.

Amazon Kinesis API errors are counted once per Amazon Kinesis record. Multiple user records within a Amazon Kinesis record do not generate multiple counts. 

----

#### All Errors

Metric Level: Summary

Unit: Count

This is triggered by the same errors as above, but does not distinguish between types. This is useful as a general monitor of the error rate without having to manually sum the counts from all the different types of errors.

----

#### Retries per Record

Metric Level: Detailed

Unit: Count

Number of retries performed per user record. Zero is emitted for records that succeed in one try.

Data is emitted at the moment a record finishes (when it either succeeds or can no longer be retried). If record TTL is set to a very large value, this metric may be significantly delayed. 

----

#### Buffering Time

Metric Level: Summary

Unit: Milliseconds

The time between a user record arriving at the KPL and leaving for the backend. This information is transmitted back to the user on a per-record basis, but is also available as an aggregated statistic.

----

#### Request Time

Metric Level: Detailed

Unit: Milliseconds

The time it takes to perform PutRecordsRequests.

----

#### User Records per Kinesis Record

Metric Level: Detailed

Unit: Count

The number of logical user records aggregated into a single Amazon Kinesis record. 

----

#### Amazon Kinesis Records per PutRecordsRequest

Metric Level: Detailed

Unit: Count

The number of Amazon Kinesis records aggregated into a single PutRecordsRequest. 

Not available at shard level.

----

#### User Records per PutRecordsRequest

Metric Level: Detailed

Unit: Count

The total number of user records contained within a PutRecordsRequest. This is roughly equivalent to the product of the previous two metrics.

Not available at shard level.

-------------

## FAQ

#### Why is the window limit 60? Why not more?

Keeping more data consumes more memory and increases read time when larger time windows are requested. There are many possible optimizations, but the effort seems unnecessary, given that CloudWatch exists for long term stats (that is, > 60s).

#### Why are there no metrics for the number of records failed?

That information is implicit in the number of records succeeded metrics. The difference between the count and the sum gives the number of failures. For alarming purposes, the user is advised to use the average, which gives the success rate, rather than the absolute value.

#### Will metrics affect performance?

Unlikely. The sort of statistics we are accumulating are extremely fast to compute. Lock contention is expected to be low because the emission of metrics is most likely randomly distributed in time in most cases.
