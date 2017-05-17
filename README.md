# Kinesis Producer Library

## Introduction

The Amazon Kinesis Producer Library (KPL) performs many tasks common to creating efficient and reliable producers for [Amazon Kinesis][amazon-kinesis]. By using the KPL, customers do not need to develop the same logic every time they create a new application for data ingestion.

For detailed information and installation instructions, see the article [Developing Producer Applications for Amazon Kinesis Using the Amazon Kinesis Producer Library][amazon-kpl-docs] in the [Amazon Kinesis Developer Guide][kinesis-developer-guide].

## Release Notes

### 0.12.4

#### Java

* Upgraded dependency on aws-java-sdk-core to 1.11.128, and removed version range.
  * [PR #84](https://github.com/awslabs/amazon-kinesis-producer/pull/84)
  * [PR #106](https://github.com/awslabs/amazon-kinesis-producer/pull/106)
* Use an explicit lock file to manage access to the native KPL binaries.
  * [Issue #91](https://github.com/awslabs/amazon-kinesis-producer/issues/91)
  * [PR #92](https://github.com/awslabs/amazon-kinesis-producer/pull/92)
* Log reader threads should be shut down when the native process exits.
  * [Issue #93](https://github.com/awslabs/amazon-kinesis-producer/issues/93)
  * [PR #94](https://github.com/awslabs/amazon-kinesis-producer/pull/94)

#### C++ Core

* Add support for using a thread pool, instead of a thread per request.
  The thread pool model guarantees a fixed number of threads, but have issue catching up if the KPL is overloaded.
  * [PR #100](https://github.com/awslabs/amazon-kinesis-producer/pull/100)
* Add log messages, and statistics about sending data to Kinesis.
  * Added flush statistics that record the count of events that trigger flushes of data destined for Kinesis
  * Added a log message that indicates the average time it takes for a PutRecords request to be completed.

      This time is recorded from the when the request is enqueued to when it is completed.
  * Log a warning if the average request time rises above five times the configured flush interval.

      If you see this warning normally it indicates that the KPL is having issues keeping up. The most likely
      cause is to many requests being generated, and you should investigate the flush triggers to determine why flushes
      are being triggered.
  * [PR #102](https://github.com/awslabs/amazon-kinesis-producer/pull/102)

### 0.12.3

#### Java

* The Java process will periodically reset the last modified times for native components. This will help to ensure that these files aren't deleted by automated cleanup scripts.
  * Fixes [Issue #81](https://github.com/awslabs/amazon-kinesis-producer/issues/81)

### 0.12.2

#### C++ Core

* The native process will no longer report SIGPIPE signals as an error with a stack trace.
* Allow the use of SIGUSR1 to trigger a stack trace. This stack trace will only report for the thread that happened to receive the signal.

### 0.12.1

#### C++ Core

* The native process will no longer attempt to use the default system CA bundle
  * The KPL should now run on versions of Linux that don't place the default CA bundle in /etc/pki
  * This fixes issue #66
  
* Added automatic BJS endpoint selection
  * The KPL will now select the BJS endpoint when configured for BJS.
  * This fixes issue #36

### 0.12.0

* **Maven Artifact Signing Change**
  * Artifacts are now signed by the identity `Amazon Kinesis Tools <amazon-kinesis-tools@amazon.com>`

* **Windows Support is not Available for this Version**.

  This version of the Kinesis Producer Library doesn't currently support windows. Windows support will be added back at a later date.

#### Java

* Log output from the kinesis_producer is now captured, and re-emitted by the LogInputStreamReader
* The daemon is now more aggressive about restarting the native kinesis_producer process.
* Updated AWS SDK dependency.

#### C++ Core

* The native process now uses version 1.0.5 of the AWS C++ SDK.
  * The native process doesn't currently support any of the AWS C++ SDK credentials providers. Support for these providers will be added a later date.
* The native process now attempts to produce stack traces for various fatal signals.


### 0.10.2

Misc bug fixes and improvements.

__Important__: Becuase the slf4j-simple dependency has been made optional, you will now need to have a logging implementation in your dependencies before the Java logs will show up. For details about slf4j, see the [manual](http://www.slf4j.org/manual.html). For a quick walkthrough on how to get basic logging, see [this page](http://saltnlight5.blogspot.com/2013/08/how-to-configure-slf4j-with-different.html).

#### General

+ The default value of the maxConnections setting has been increased from 4 to 24.

#### Java

+ slf4j-simple dependency is now optional.
+ `aws-java-sdk-core` version increased to `1.10.34`. Please ensure your AWS SDK components all have the same major version.
+ Record completion callbacks are now executed in a threadpool rather than on the IPC thread.

#### C++ Core

+ Fixed bug that produced invalidly signed requests on the latest version of OSX.

--------------

### 0.10.1

Bug fixes and improved temp file management in Java wrapper.

#### Java

+ The wrapper no longer creates unique a copy of the native binary on disk per instance of KinesisProducer. Multiple instances can now share the same file. Clobbering between versions is prevented by adding the hash of the contents to the file name.

#### C++ Core

+ Idle CPU usage has been reduced (Issue [15](https://github.com/awslabs/amazon-kinesis-producer/issues/15))
+ The native process should now terminate when the wrapper process is killed (Issues [14](https://github.com/awslabs/amazon-kinesis-producer/issues/14), [16](https://github.com/awslabs/amazon-kinesis-producer/issues/16))

--------------

### 0.10.0

Significant platform compatibility improvements and easier credentials configuration in the Java wrapper.

#### General

+ The KPL now works on Windows (Server 2008 and later)
+ The lower bound on the `RecordMaxBufferedTime` config has been removed. You can now set it to 0, although this is discouraged

#### Java

+ The java packages have been renamed to be consistent with the package names of the KCL (it's now com.amazonaws.__services__.kinesis.producer).
+ The `Configuration` class has been renamed `KinesisProducerConfiguration`.
+ `KinesisProducerConfiguration` now accepts the AWS Java SDK's `AWSCredentialsProvider` instances for configuring credentials.
+ In addition, a different set of credentials can now be provided for uploading metrics.

#### C++ Core

+ Glibc version requirement has been reduced to 2.5 (from 2.17).
+ The binary is now mostly statically linked, such that configuring `(DY)LD_LIBRARY_PATH` should no longer be necessary.
+ No longer uses `std::shared_timed_mutex`, so updating libc++ on OS X is no longer necessary
+ Removed dependencies on glog, libunwind and gperftools.

#### Bug Fixes

+ Error messages related to closing sockets ([Issue 3](https://github.com/awslabs/amazon-kinesis-producer/issues/3))
+ Queued requests can now expire ([Issue 4](https://github.com/awslabs/amazon-kinesis-producer/issues/4))

--------------

### 0.9.0
+ First release

## Supported Platforms and Languages

The KPL is written in C++ and runs as a child process to the main user process. Precompiled native binaries are bundled with the Java release and are managed by the Java wrapper.

The Java package should run without the need to install any additional native libraries on the following operating systems:

+ Linux distributions with kernel 2.6.18 (September 2006) and later
+ Apple OS X 10.9 and later
+ Windows Server 2008 and later

Note the release is 64-bit only.

[kinesis-developer-guide]: http://docs.aws.amazon.com/kinesis/latest/dev/introduction.html
[amazon-kinesis]: http://aws.amazon.com/kinesis
[amazon-kpl-docs]: http://docs.aws.amazon.com/kinesis/latest/dev/developing-producers-with-kpl.html

## Sample Code

A sample java project is available in `java/amazon-kinesis-sample`.

## Compiling the Native Code

Rather than compiling from source, Java developers are encouraged to use the [KPL release in Maven](https://search.maven.org/#search%7Cga%7C1%7Camazon-kinesis-producer), which includes pre-compiled native binaries for Linux, macOS.

Compilation of the native KPL components is currently migrating to CMake. Updating of the build instructions is currently being tracked in [Issue #67](https://github.com/awslabs/amazon-kinesis-producer/issues/67).

### Using the Java Wrapper with the Compiled Native Binaries

There are two options. You can either pack the binaries into the jar like we did for the official release, or you can deploy the native binaries separately and point the java code at it.

#### Packing Native Binaries into the Jar

You will need JDK 1.7+, Apache Maven and Python 2.7 installed.

If you're on Windows, do the following in the git bash shell we used for building. You will need to add `java` and `python` to the `PATH`, as well as set `JAVA_HOME` for maven to work. 

Run `python pack.py`

Then

```
pushd java/amazon-kinesis-producer
mvn clean package source:jar javadoc:jar install
popd
```
This installs the jar into your local maven repo. The jar itself is available in `java/amazon-kinesis-producer/targets/`

The java wrapper contains logic that will extract and run the binaries during initialization.

#### Pointing the Java wrapper at a Custom Binary

The `KinesisProducerConfiguration` class provides an option `setNativeExecutable(String val)`. You can use this to provide a path to the `kinesis_producer[.exe]` executable you have built. You have to use backslashes to delimit paths on Windows if giving a string literal.

