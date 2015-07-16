# KPL Java Sample Application

## Setup

You will need the following:

+ A stream to put into (with any number of shards). There should be no previous data in the stream, creating a new stream is recommended.
+ AWS credentials, preferably an IAM role if using EC2
+ JDK
+ Maven (```brew install maven```, ```sudo apt-get install maven```, [Amazon Liunx](https://gist.github.com/sebsto/19b99f1fa1f32cae5d00))

Since we'll be running a consumer as well as a producer, the credentials/IAM role should contain permissions for DynamoDB and CloudWatch, in addition to Kinesis. See the [official guide](http://docs.aws.amazon.com/kinesis/latest/dev/learning-kinesis-module-one-iam.html) for details.

If running locally, set environment variables for the AWS credentials:

```
export AWS_ACCESS_KEY_ID=...
export AWS_SECRET_KEY=...
```

If running in EC2, the KPL will automatically retrieve credentials from any associated IAM role.

You can also pass credentials to the KPL programmatically during initialization.

## Edit the Sample Application

The sample application is a Maven project. It takes dependencies on the KPL and KCL and contains both a producer and consumer.

Refer to the documentation within the code files for more details about what it's actually doing.

You will need to modify the stream name and region variables in the producer code to point the app at your own stream.

In ```SampleProducer.java```, look for:

```
/**
 * Change this to your stream name.
 */
public static final String STREAM_NAME = "test";

/**
 * Change this to the region you are using.
 * 
 * We do not use the SDK Regions enum because the KPL does not depend on the
 * SDK.
 */
public static final String REGION = "us-west-1";
```

## Run the Sample

After you've made the necessary changes, do a clean build:

```
mvn clean package
```

Then run the producer to put some data into your stream:

```
mvn exec:java -Dexec.mainClass="com.amazonaws.services.kinesis.producer.sample.SampleProducer"
```

Finally run the consumer to retrieve that data:

```
mvn exec:java -Dexec.mainClass="com.amazonaws.services.kinesis.producer.sample.SampleConsumer"
```
