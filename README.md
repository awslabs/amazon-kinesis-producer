# Kinesis Producer Library

## Introduction

The Amazon Kinesis Producer Library (KPL) performs many tasks common to creating efficient and reliable producers for [Amazon Kinesis][amazon-kinesis]. By using the KPL, customers do not need to develop the same logic every time they create a new application for data ingestion.

For detailed information and installation instructions, see the article [Developing Producer Applications for Amazon Kinesis Using the Amazon Kinesis Producer Library][amazon-kpl-docs] in the [Amazon Kinesis Developer Guide][kinesis-developer-guide].

## Recommended Upgrade
Starting with release 0.14.0, KPL will switch to using ListShards API instead of the DescribeStream API. This API is more scaleable and will result in less throttling problems when your KPL fleet needs to update it's shard map.

## Required Upgrade
Starting on February 9, 2018 Amazon Kinesis Data Streams will begin transitioning to certificates issued by [Amazon Trust Services (ATS)](https://www.amazontrust.com/).  To continue using the Kinesis Producer Library (KPL) you must upgrade the KPL to version [0.12.6](http://search.maven.org/#artifactdetails|com.amazonaws|amazon-kinesis-producer|0.12.6|jar) or [later](http://search.maven.org/#search%7Cgav%7C1%7Cg%3A%22com.amazonaws%22%20AND%20a%3A%22amazon-kinesis-producer%22).

If you have further questions [please open a GitHub Issue](https://github.com/awslabs/amazon-kinesis-producer/issues), or [create a case with the AWS Support Center](https://console.aws.amazon.com/support/v1#/case/create).

This is a restatement of the [notice published](https://docs.aws.amazon.com/streams/latest/dev/kinesis-kpl-upgrades.html) in the [Amazon Kinesis Data Streams Developer Guide][kinesis-developer-guide]

## Release Notes
### 0.14.0
* **Note:** Windows platform will be unsupported going forward for this library.
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
+ Apple OS X 10.9 and later

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
