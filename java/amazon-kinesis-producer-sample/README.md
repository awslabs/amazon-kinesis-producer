# KPL Java Sample Application

## Setup

You will need the following:

+ A stream to put into (with any number of shards). There should be no previous data in the stream, creating a new stream is recommended.
+ AWS credentials, preferably an IAM role if using EC2
+ JDK
+ Maven (```brew install maven```, ```sudo apt-get install maven```, [Amazon Linux](https://gist.github.com/sebsto/19b99f1fa1f32cae5d00))

Since we'll be running a consumer as well as a producer, the credentials/IAM role should contain permissions for DynamoDB and CloudWatch, in addition to Kinesis. See the [official guide](http://docs.aws.amazon.com/kinesis/latest/dev/learning-kinesis-module-one-iam.html) for details.

If running locally, set environment variables for the AWS credentials:

```
export AWS_ACCESS_KEY_ID=...
export AWS_SECRET_KEY=...
```

If running in EC2, the KPL will automatically retrieve credentials from any associated IAM role.

You can also pass credentials to the KPL programmatically during initialization.

## Run the Sample Application

The sample application is a Maven project. It takes dependencies on the KPL and KCL and contains both a producer and consumer.

Refer to the documentation within the code files for more details about what it's actually doing.

The sample producer takes optional positional parameters for the stream name, region and duration of the test in seconds. The default stream name is ``test`` and default region is ``us-west-1``.

The sample consumer takes optional positional parameters for the stream name and region.

Build the sample application:

```
mvn clean package
```

Then run the producer to put some data into a stream called ``kpltest`` in ``us-west-2`` for ``100 seconds``:

```
mvn exec:java -Dexec.mainClass="software.amazon.kinesis.producer.sample.SampleProducer" -Dexec.args="kpltest us-west-2 100"
```

Finally run the consumer to retrieve the data from a stream called ``kpltest`` in ``us-west-2``:

```
mvn exec:java -Dexec.mainClass="software.amazon.kinesis.producer.sample.SampleConsumer" -Dexec.args="kpltest us-west-2"
```
