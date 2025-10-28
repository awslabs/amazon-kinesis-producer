# Kinesis Producer Library

>[!Important]
> ### Amazon Kinesis Producer Library (KPL) 0.x will reach end-of-support on January 30, 2026
> Amazon Kinesis Producer Library (KPL) 0.x will reach end-of-support on January 30, 2026. Accordingly, the version will enter maintenance mode on April 17, 2025. During maintenance mode, AWS will provide updates only for critical bug fixes and security issues. Major versions in maintenance mode will not receive updates for new features or feature enhancements. If you’re using KPL 0.x, we recommend migrating to the latest version. When migrating from KPL 0.x to 1.x, you can upgrade your current KPL application without any change in your data processing logic. For details about the end-of-support notice and required actions, see the following links:
> 
> * [AWS Blog: Announcing end-of-support for Amazon Kinesis Client Library 1.x and Amazon Kinesis Producer Library 0.x effective January 30, 2026](https://aws.amazon.com/blogs/big-data/announcing-end-of-support-for-amazon-kinesis-client-library-1-x-and-amazon-kinesis-producer-library-0-x-effective-january-30-2026/)
> * [Kinesis documentation: KPL version lifecycle policy](https://docs.aws.amazon.com/streams/latest/dev/kpl-version-lifecycle-policy.html)
> * [Kinesis documentation: Migrate from KPL 0.x to KPL 1.x](https://docs.aws.amazon.com/streams/latest/dev/kpl-migration-1x.html)

## Introduction

The Amazon Kinesis Producer Library (KPL) performs many tasks common to creating efficient and reliable producers for [Amazon Kinesis][amazon-kinesis]. By using the KPL, customers do not need to develop the same logic every time they create a new application for data ingestion.

For detailed information and installation instructions, see the article [Developing Producer Applications for Amazon Kinesis Using the Amazon Kinesis Producer Library][amazon-kpl-docs] in the [Amazon Kinesis Developer Guide][kinesis-developer-guide].

## Back-pressure
Please see [this blog post](https://aws.amazon.com/blogs/big-data/implementing-efficient-and-reliable-producers-with-the-amazon-kinesis-producer-library/) for details about writing efficient and reliable producers using the KPL. This blogpost contains details about overhead in various situations in which you might be using the KPL including back-pressure considerations.

The KPL can consume enough memory to crash itself if it gets pushed too many records without time to process them. As a protection against this, we ask that every customer implement back-pressure to protect the KPL process. Once the KPL starts getting too many records in it's buffer it will spend most of it's CPU cycles on record management, rather than record processing making the problem worse. This is highly dependent on the customer record sizes, rates, configurations, host CPU and memory limits.

_When deciding the limits of your KPL instance, please consider your MAX record size, MAX request rate spikes, host memory availability, and TTL. If you are buffering requests before going into the KPL, consider that as well since that still puts memory pressure on the host system. If the KPL buffer grows too large it may be forcibly crashed due to memory exhaustion._

Sample Back-pressure implementation:

````
ClickEvent event = inputQueue.take();
        String partitionKey = event.getSessionId();
        String payload =  event.getPayload();
        ByteBuffer data = ByteBuffer.wrap(payload.getBytes("UTF-8"));
        while (kpl.getOutstandingRecordsCount() > MAX_RECORDS_IN_FLIGHT) {
            Thread.sleep(SLEEP_BACKOFF_IN_MS);
        }
        recordsPut.getAndIncrement();

        ListenableFuture<UserRecordResult> f =
                kpl.addUserRecord(STREAM_NAME, partitionKey, data);
        Futures.addCallback(f, new FutureCallback<UserRecordResult>() {
          ...
          ...
````

_Sample above is provided as an example implementation. Please take your application and use cases into consideration before applying logic_

## Recommended Upgrade for All Users of 0.15.0 - 0.15.6 Amazon Kinesis Producer
⚠️ It's highly recommended for users of version 0.15.0 - 0.15.6 of the Amazon Kinesis Producer to upgrade to version [0.15.7 or later](https://central.sonatype.com/search?q=a%3Aamazon-kinesis-producer&smo=true). We recommend you to use the latest version of KPL. A bug has been identified in versions prior from 0.15.0 - 0.15.6 is causing memory leak issue.

ℹ️ Amazon Kinesis Producer versions prior to 0.15.0 are not impacted.

## Recommended Settings for Streams larger than 800 shards
The KPL is an application for ingesting data to your Kinesis Data Streams. As your streams grow you may find the need to tune the KPL to enable it to accommodate the growing needs of your applications. Without optimized configurations your KPL processes will see inefficient CPU usage and delays in writing records into KDS. For streams larger than 800 shards, we recommend the following settings:

* ThreadingModel= “POOLED”
* MetricsGranularity= “stream”
* ThreadPoolSize=128

_We recommend performing sufficient testing before applying these changes to production, as every customer has different usage patterns_

## Required KPL Update – Changes in v0.15.0
KPL 0.15.0 now incorporates StreamARN in the Kinesis requests, such as PutRecords and ListShards, to take advantage of Kinesis Data Streams (KDS) enhanced availability as the result of service cellularization. Version 0.15.0 adds STS as the new dependency; by using STS, customers can benefit from StreamARN without modifying any code.

## Required KPL Update – Changes in v0.14.0
KPL 0.14.0 now uses ListShards API, making it easier for your Kinesis Producer applications to scale. Kinesis Data Streams (KDS) enables you to scale your stream capacity without any changes to producers and consumers. After a scaling event, producer applications need to discover the new shard map. Version 0.14.0 replaces the DescribeStream with the ListShards API for shard discovery. ListShards API supports 100 TPS per stream compared to DescribeStream that supports 10 TPS per account. For an account with 10 streams using KPL v0.14.0 will provide you a 100X higher call rate for shard discovery, eliminating the need for a DescribeStream API limit increase for scaling. You can find more information on the [ListShards API ](https://docs.aws.amazon.com/kinesis/latest/APIReference/API_ListShards.html) in the Kinesis Data Streams documentation.


## Required Upgrade
Starting on February 9, 2018 Amazon Kinesis Data Streams will begin transitioning to certificates issued by [Amazon Trust Services (ATS)](https://www.amazontrust.com/).  To continue using the Kinesis Producer Library (KPL) you must upgrade the KPL to version [0.12.6 or later](https://central.sonatype.com/search?q=a%3Aamazon-kinesis-producer&smo=true). We recommend you to use the latest version of KPL.

If you have further questions [please open a GitHub Issue](https://github.com/awslabs/amazon-kinesis-producer/issues), or [create a case with the AWS Support Center](https://console.aws.amazon.com/support/v1#/case/create).

This is a restatement of the [notice published](https://docs.aws.amazon.com/streams/latest/dev/kinesis-kpl-upgrades.html) in the [Amazon Kinesis Data Streams Developer Guide][kinesis-developer-guide]

## Release Notes

## 1.0.6
* [#677](https://github.com/awslabs/amazon-kinesis-producer/pull/677) Adds support for record sizes up to 10MiB
* [#676](https://github.com/awslabs/amazon-kinesis-producer/pull/676) Upgrade openssl, curl, aws-sdk-cpp

## 1.0.5
* [#671](https://github.com/awslabs/amazon-kinesis-producer/pull/671) Removing STS from native code
* [#666](https://github.com/awslabs/amazon-kinesis-producer/pull/666) Add constructor for UserRecordFailedException for backwards compatibility

## 1.0.4
* [#660](https://github.com/awslabs/amazon-kinesis-producer/pull/660) Upgrade C++ dependencies to latest
* [#659](https://github.com/awslabs/amazon-kinesis-producer/pull/659) Add ability for users to fetch the UserRecord in onFailure
* [#657](https://github.com/awslabs/amazon-kinesis-producer/pull/657) Allow coredumps to be disabled based on user preference
* [#655](https://github.com/awslabs/amazon-kinesis-producer/pull/655) Upgrade org.apache.commons:commons-lang3 from 3.14.0 to 3.18.0

## 1.0.3
* [#653](https://github.com/awslabs/amazon-kinesis-producer/pull/653) Upgrade glue schema-registry-serde from 1.1.23 to 1.1.24

## 1.0.2
* [#637](https://github.com/awslabs/amazon-kinesis-producer/pull/637) Add a feature flag to enable/disable oldest future tracker
* [#648](https://github.com/awslabs/amazon-kinesis-producer/pull/648) Update metrics manager to flush all metrics to prevent metrics accumulation in memory

## 1.0.1
* [#638](https://github.com/awslabs/amazon-kinesis-producer/pull/638) Fix race condition that may cause empty metric uploads
* [#635](https://github.com/awslabs/amazon-kinesis-producer/pull/635) Fix OpenSSL build for upgrade
* [#632](https://github.com/awslabs/amazon-kinesis-producer/pull/632) Update README.md with KPL 0.x end-of-support notice
* [#630](https://github.com/awslabs/amazon-kinesis-producer/pull/630) Upgrade org.slf4j:slf4j-api from 2.0.0 to 2.0.17
* [#627](https://github.com/awslabs/amazon-kinesis-producer/pull/627) Upgrade dependency versions to fix vulnerabilities
  * Upgrade curl to 8.12.0
  * Upgrade openssl to 3.4.0
  * Upgrade zlib to 1.3.1
  * Upgrade schema-registry-serde to 1.1.23
* [#623](https://github.com/awslabs/amazon-kinesis-producer/pull/623) Update SampleProducer.java with guidance on calling flushSync()
* [#619](https://github.com/awslabs/amazon-kinesis-producer/pull/619) Update README.md with guidance on recommended upgrade

## 1.0.0
Check [this AWS Kinesis Data Streams Developer Guide](https://docs.aws.amazon.com/streams/latest/dev/kpl-migration-1x.html) to migrate to KPL 1.0.0 from previous KPL verions.
* [#614](https://github.com/awslabs/amazon-kinesis-producer/pull/614) Upgrade to AWS SDK for Java 2.x
    * Upgrade protobuf-java to 4.29.0
    * Upgrade schema-registry-serde to 1.1.22
    * Upgrade jackson-core to 2.18.2
    * Upgrade jackson-databind to 2.18.2
* [#611](https://github.com/awslabs/amazon-kinesis-producer/pull/611) Make certificate authority file configurable in KinesisProducerConfiguration
* [#615](https://github.com/awslabs/amazon-kinesis-producer/pull/615) Refactor groupId to software.amazon.kinesis
* [#616](https://github.com/awslabs/amazon-kinesis-producer/pull/616) Auto generate protobuf files in C++
* [#484](https://github.com/awslabs/amazon-kinesis-producer/pull/484) Upgrade maven-compiler-plugin to 3.11.0
* [#607](https://github.com/awslabs/amazon-kinesis-producer/pull/607) Upgrade hibernate-validator to 6.2.0.Final

## 0.15.12
* [#593](https://github.com/awslabs/amazon-kinesis-producer/pull/593) Replace all usage of sys_siglist with strsignal as sys_siglist is deprecated
* [#594](https://github.com/awslabs/amazon-kinesis-producer/pull/594) Check if dimension.value is blank before using it in metrics manager
* [#596](https://github.com/awslabs/amazon-kinesis-producer/pull/596) Update getOldestRecordTimeMillis to avoid potential NullPointerException
* [#600](https://github.com/awslabs/amazon-kinesis-producer/pull/600) Fix build issue after cpp sdk upgrade
* [#597](https://github.com/awslabs/amazon-kinesis-producer/pull/597) Bump cpp sdk from 1.11.62 to 1.11.420 and java sdk from 1.12.772 to 1.12.773
* [#570](https://github.com/awslabs/amazon-kinesis-producer/pull/570) Bump commons-lang from 2.6 to 3.14.0
* [#598](https://github.com/awslabs/amazon-kinesis-producer/pull/598) Bump commons-io:commons-io from 2.13.0 to 2.17.0 in /java/amazon-kinesis-producer to address CVE vulnerability
* [#589](https://github.com/awslabs/amazon-kinesis-producer/pull/589) Bump com.google.protobuf:protobuf-java from 3.21.12 to 3.25.5 in /java/amazon-kinesis-producer to address CVE vulnerability
* [#579](https://github.com/awslabs/amazon-kinesis-producer/pull/579) Bump com.google.guava:guava from 31.1-jre to 33.3.0-jre in /java/amazon-kinesis-producer to address CVE vulnerability
* [#588](https://github.com/awslabs/amazon-kinesis-producer/pull/588) Bump com.amazonaws:aws-java-sdk-core from 1.12.382 to 1.12.772 in /java/amazon-kinesis-producer and java/amazon-kinesis-producer-sample

## 0.15.11
* [#576](https://github.com/awslabs/amazon-kinesis-producer/pull/576) Improve retry logic during stream scaling
* [#571](https://github.com/awslabs/amazon-kinesis-producer/pull/571) Upgrade ch.qos.logback:logback-classic from 1.3.0 to 1.3.12

## 0.15.10
* [#560 ](https://github.com/awslabs/amazon-kinesis-producer/pull/560) Reverting to remove a bug with using Stream ARN. Please stay tuned for a future release before using Stream ARN.
* [#526](https://github.com/awslabs/amazon-kinesis-producer/pull/526) Drop dependency on jaxb for converting binary arrays to hex
* [1.1.18](https://github.com/awslabs/amazon-kinesis-producer/commit/3de737329f85e837c5992c57642f35d461ecfb8a)Update GSR dependency - To address CVE vulnerability


## 0.15.9
* [#552](https://github.com/awslabs/amazon-kinesis-producer/pull/552) Add StreamARN parameter to support CAA
    * StreamARN parameter can be now be used to benefit from Cross account access for KPL requests.
    
## 0.15.8
* [#537](https://github.com/awslabs/amazon-kinesis-producer/pull/537) Update to latest version of Glue Schema Registry library

## 0.15.7
* [#498](https://github.com/awslabs/amazon-kinesis-producer/pull/498) Fix some memory leak cases in legacy code
  * Upgrade SDK version to avoid s2n_cleanup related memory leak 
  * Fix resource cleanup on KPL end to avoid memory leak

## 0.15.6
* [#490](https://github.com/awslabs/amazon-kinesis-producer/pull/490) Updating aws cpp sdk version

### 0.15.5
* [#482](https://github.com/awslabs/amazon-kinesis-producer/pull/482)  Remove the stream arn parameter when the next token is present

### 0.15.4

### 0.15.3
* [#478](https://github.com/awslabs/amazon-kinesis-producer/pull/478) Update AWS SDK CPP version

### 0.15.2
* [#471](https://github.com/awslabs/amazon-kinesis-producer/pull/471) Upgrade Java dependencies

### 0.15.1
* [#469](https://github.com/awslabs/amazon-kinesis-producer/pull/469) Use AWS CodeBuild to compile C++ binary

### 0.15.0
* [#465](https://github.com/awslabs/amazon-kinesis-producer/pull/465)
  * Revert the upgrade of jakarta.xml.bind to be backward-compatible with Java8
  * Add more logs to verify that IMDSV2 is used correctly for getting region info for KPL running in EC2 instances
* [#463](https://github.com/awslabs/amazon-kinesis-producer/pull/463)
  * Use sts to construct stream arn
  * Exit KPL if STS call fails to avoid dual mode
  * Deprecate IMDSv1 calls for obtaining EC2 metadata
* [#444](https://github.com/awslabs/amazon-kinesis-producer/pull/444)
  *  Update bootstrap.sh to work on three platforms



### 0.14.13
* [#440](https://github.com/awslabs/amazon-kinesis-producer/pull/440)
  * Upgrade the dependencies used in bootstrap + Java dependencies
  * Correct the log level discrepancy for the warnings

### 0.14.12
* [#425](https://github.com/awslabs/amazon-kinesis-producer/pull/425) Fix build issues in CI
* [#424](https://github.com/awslabs/amazon-kinesis-producer/pull/424) Fix build issues in CI
* [#423](https://github.com/awslabs/amazon-kinesis-producer/pull/423) Upgrade GSR version to 1.1.9
* [#420](https://github.com/awslabs/amazon-kinesis-producer/pull/420) Fix cpp branch
* [#419](https://github.com/awslabs/amazon-kinesis-producer/pull/419) Fix aws-cpp branch
* [#418](https://github.com/awslabs/amazon-kinesis-producer/pull/418) Fix travis build
* [#416](https://github.com/awslabs/amazon-kinesis-producer/pull/416) Configure dependabot
* [#415](https://github.com/awslabs/amazon-kinesis-producer/pull/415) Fix travis build
* [#414](https://github.com/awslabs/amazon-kinesis-producer/pull/414) Fix travis build

### 0.14.11
* [#409](https://github.com/awslabs/amazon-kinesis-producer/pull/409) Bump protobuf-java from 3.11.4 to 3.16.1 in /java/amazon-kinesis-producer
* [#408](https://github.com/awslabs/amazon-kinesis-producer/pull/408) Update curl version from 7.77 to 7.81
* [#395](https://github.com/awslabs/amazon-kinesis-producer/pull/395) Configure dependabot
* [#391](https://github.com/awslabs/amazon-kinesis-producer/pull/391) Fixing travis build issues
* [#388](https://github.com/awslabs/amazon-kinesis-producer/pull/388) Fixing build issues due to stale CA certs

### 0.14.10
* [#386](https://github.com/awslabs/amazon-kinesis-producer/pull/386) Upgraded Glue schema registry from 1.1.1 to 1.1.5
* [#384](https://github.com/awslabs/amazon-kinesis-producer/pull/384) Upgraded logback-classic from 1.2.0 to 1.2.6
* [#323](https://github.com/awslabs/amazon-kinesis-producer/pull/323) Upgraded junit from 4.12 to 4.13.1

### 0.14.9
* [#370](https://github.com/awslabs/amazon-kinesis-producer/pull/370) Upgraded build script dependencies
  * Upgraded version of openssl from 1.0.1m to 1.0.2u
  * Upgraded version of boost from 1.61 to 1.76
  * Upgraded version of zlib from 1.2.8 to 1.2.11
* [#377](https://github.com/awslabs/amazon-kinesis-producer/pull/377) Added an optimization to filter out closed shards.

### 0.14.8
* [PR #331](https://github.com/awslabs/amazon-kinesis-producer/pull/331) Fixed a typo in README.md
* [PR #363](https://github.com/awslabs/amazon-kinesis-producer/pull/363) Upgrading hibernate-validator to 6.0.20.Final
* [PR #365](https://github.com/awslabs/amazon-kinesis-producer/pull/365) Upgrading logback-classic to 1.2.0
* [PR #367](https://github.com/awslabs/amazon-kinesis-producer/pull/367) Upgrading Glue Schema Registry to 1.1.1

### 0.14.7
* [PR #350](https://github.com/awslabs/amazon-kinesis-producer/pull/350/files) Upgrading Guava to 29.0-jre
* [PR #352](https://github.com/awslabs/amazon-kinesis-producer/pull/352/files) Upgrading Commons IO to 2.7
* [PR #351](https://github.com/awslabs/amazon-kinesis-producer/pull/351) Adding support for proxy configurations
* [PR #356](https://github.com/awslabs/amazon-kinesis-producer/pull/356) Fixing build issues in Travis CI

### 0.14.6
* [PR #341] Updating Java SDK version in KPL to 1.11.960.

### 0.14.5
* [PR #339] Fixing KPL not emmiting Kinesis PutRecords call context metrics.

### 0.14.4
* [PR #334] Add support for building multiple architectures, specifically arm64.
   * This now supports AWS Graviton based instances.
   * Bumped Boost slightly to a version that includes Arm support and added the architecture to the path for kinesis_producer.
* [PR #335] Fixed logging for native layer allowing to enable debug/trace logs.

### 0.14.3
* [PR #327] Adding support for timeout on user records at Java layer.
   * New optional KPL config parameter userRecordTimeoutInMillis which can be used to timeout records at the java layer queued for processing.
* [PR #328] Changing CloudWatch client retry strategy to use default SDK retry strategy with exponential backoff.
* [PR #324] Adding KPL metric to track the time for oldest user record in processing at the java layer.
* [PR #318] Fixing bug where KPL goes into a continuous retry storm if the stream is deleted and re-created.

### 0.14.2
* [PR #320] Adding support for Glue Schema Registry.
   * Serialize and send schemas along with records, support for compression and auto-registration of schemas.
* [PR #316] Bumping junit from 4.12 to 4.13.1
* [PR #312] Adding new parameter in KPL config to allow cert path to be overridden.
* [PR #310] Fixing bug to make the executor service to use 4*num_cores threads.
* [PR #307] Dependency Upgrade
  * Upgrade Guava to 26.0-jre
  * Update BOOST C++ Libraries link as cert expired on the older link



### 0.14.1
* [PR #302] Dependency Upgrade
  * upgrade org.hibernate.validator:hibernate-validator 6.0.2.Final -> 6.0.18.Final
  * upgrade com.google.guava:guava 18.0 -> 24.1.1-jre
* [PR #300] Fix Travis CI build issues
* [PR #298] Upgrade google-protobuf to 3.11.4

### 0.14.0
* **Note:** Windows platform will be unsupported going forward for this library.
* [PR #280] When aggregation is enabled and all the buffer time is consumed for aggregating User records into Kinesis records, allow some additional buffer time for aggregating Kinesis Records into PutRecords calls.
* [PR #260] Added endpoint for China Ningxia region (cn-northwest-1).
* [PR #277] Changed mechanism to update the shard map
  * Switched to using ListShards instead of DescribeStream, as this is a more scalable API
  * Reduced the number of unnecessary shard map invalidations
  * Reduced the number of unnecessary update shard map calls
  * Reduced logging noise for aggregated records landing on an unexpected shard
* [PR #276] Updated AWS SDK from 1.0.5 to 1.7.180
* [PR #275] Improved the sample code to avoid need to edit code to run.
* [PR #274] Updated bootstrap.sh to build all dependencies and pack binaries into the jar.
* [PR #273] Added compile flags to enable compiling aws-sdk-cpp with Gcc7.
* [PR #229] Fixed bootstrap.sh to download dependent libraries directly from source.
* [PR #246] [PR #264] Various Typos

### 0.13.1

* Including windows binary for Apache 2.0 release.

### 0.13.0

* [PR #256] Update KPL to Apache 2.0

### 0.12.11

#### Java

* Bump up the version to 0.12.11.
  * Fixes [Issue #231](https://github.com/awslabs/amazon-kinesis-producer/issues/231)

Older release notes moved to CHANGELOG.md

## Supported Platforms and Languages

The KPL is written in C++ and runs as a child process to the main user process. Precompiled native binaries are bundled with the Java release and are managed by the Java wrapper.

The Java package should run without the need to install any additional native libraries on the following operating systems:

+ Linux distributions with glibc 2.9 or later
+ Apple OS X 10.13 and later

Note the release is 64-bit only.

[kinesis-developer-guide]: http://docs.aws.amazon.com/kinesis/latest/dev/introduction.html
[amazon-kinesis]: http://aws.amazon.com/kinesis
[amazon-kpl-docs]: http://docs.aws.amazon.com/kinesis/latest/dev/developing-producers-with-kpl.html
[amazon-reliable-producers]: https://aws.amazon.com/blogs/big-data/implementing-efficient-and-reliable-producers-with-the-amazon-kinesis-producer-library/

## Sample Code

A sample java project is available in `java/amazon-kinesis-sample`.

## Compiling the Native Code

Rather than compiling from source, Java developers are encouraged to use the [KPL release in Maven](https://search.maven.org/#search%7Cga%7C1%7Camazon-kinesis-producer), which includes pre-compiled native binaries for Linux, macOS.

To build the native components and bundle them into the jar, you can run the `./bootstrap.sh` which will download the dependencies, build them, then build the native binaries, bundle them into the java resources folder, and then build the java packages.  This must be done on the platform you are planning to execute the jars on.

### Using the Java Wrapper with the Compiled Native Binaries

There are two options. You can either pack the binaries into the jar like we did for the official release, or you can deploy the native binaries separately and point the java code at it.

#### Pointing the Java wrapper at a Custom Binary

The `KinesisProducerConfiguration` class provides an option `setNativeExecutable(String val)`. You can use this to provide a path to the `kinesis_producer[.exe]` executable you have built. You have to use backslashes to delimit paths on Windows if giving a string literal.
