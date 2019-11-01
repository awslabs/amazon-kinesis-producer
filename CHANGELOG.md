## Changelog
### 0.14.0

* Note: Windows will be unsupported going forward for this library.
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
* Various Typos
  * [PR #246]
  * [PR #264]
### 0.13.1

* Including windows binary for Apache 2.0 release.

### 0.13.0

* [PR #256] Update KPL to Apache 2.0

### 0.12.11

#### Java

* Bump up the version to 0.12.11.
  * Fixes [Issue #231](https://github.com/awslabs/amazon-kinesis-producer/issues/231)

### 0.12.10

#### Java
* Support for additional AWS regions.
* Bug fix to avoid Heap Out of Memory Exception.
  * [PR #225](https://github.com/awslabs/amazon-kinesis-producer/pull/225)
  * [Issue #224](https://github.com/awslabs/amazon-kinesis-producer/issues/224)

#### C++ Core
* Support for additional AWS regions.
* Update the CloudWatch upload logic to timeout retries at 10 Minutes instead of 30 Minutes and backoff between retries.

### 0.12.9

#### Java
* Stream the native application to disk instead of loading into memory.
  Extracting the native component will now stream it to disk, instead of copying it into memory.  This should reduce
  memory use of the KPL during startup.  
  * [PR #198](https://github.com/awslabs/amazon-kinesis-producer/pull/198)
* Extract certificates when using a custom binary.
  Certificates will now be extracted to the directory of the custom binary.  
  * [PR #198](https://github.com/awslabs/amazon-kinesis-producer/pull/198)
  * [Issue #161](https://github.com/awslabs/amazon-kinesis-producer/issues/161)
* Improve exception handling in the credential update threads.
  Runtime exceptions are now caught, and ignored while updating the credentials.  This should avoid the thread death
  that could occur if the credentials supplier threw an exception.  At this only `RuntimeException`s are handled,
  `Throwable`s will still cause the issue.  
  * [PR #199](https://github.com/awslabs/amazon-kinesis-producer/pull/199)
  * [Issue #49](https://github.com/awslabs/amazon-kinesis-producer/issues/49)
  * [Issue #34](https://github.com/awslabs/amazon-kinesis-producer/issues/34)

#### C++ Core
* Removed the spin lock protecting credentials access.
  Credential access is now handled by atomic swaps, and when necessary a standard explicit lock.  This significantly
  reduces contention retrieving credentials when a large number of threads are being used.  
  * [PR #191](https://github.com/awslabs/amazon-kinesis-producer/pull/191)
  * [PR #202](https://github.com/awslabs/amazon-kinesis-producer/pull/202)
  * [Issue #183](https://github.com/awslabs/amazon-kinesis-producer/issues/183)
* Ticket spin locks will now fall back to standard locking after a set number of spins.
  The ticket spin lock is now a hybrid spin lock.  After a set number of spins the lock will switch to a conventional
  lock, and condition variable.  This reduces CPU utilization when a large number of threads are accessing the
  spin lock.  
  * [PR #193](https://github.com/awslabs/amazon-kinesis-producer/pull/193)
  * [Issue #183](https://github.com/awslabs/amazon-kinesis-producer/issues/183)

### 0.12.8

#### Java
* Ensure that all certificates are registered the `FileAgeManager` to prevent file sweepers from removing them  
  * [PR #165](https://github.com/awslabs/amazon-kinesis-producer/pull/165)
* Upgrade aws-java-sdk-core to 1.11.245  
  * [Issue #164](https://github.com/awslabs/amazon-kinesis-producer/issues/164)
  * [PR #166](https://github.com/awslabs/amazon-kinesis-producer/pull/166)

### 0.12.7

#### C++ Core
* Removed unnecessary libidn, and correctly set libuuid to static linking.  This issue only affected the Linux version.  
  * [Issue #158](https://github.com/awslabs/amazon-kinesis-producer/issues/158)
  * [PR #157](https://github.com/awslabs/amazon-kinesis-producer/pull/157)
* Disabled clock_gettime support in Curl for macOS.  
  This fixes an issue where the KPL was unable to run on macOS versions older than 10.12.
  * [Issue #117](https://github.com/awslabs/amazon-kinesis-producer/issues/117)
  * [PR #159](https://github.com/awslabs/amazon-kinesis-producer/pull/159)
* Updated requirements for the using the KPL on Linux.  
  The KPL on Linux now requires glibc 2.9 or later.

### 0.12.6

#### C++ Core
* Added Windows support  
  The 0.12.x version now supports Windows.  
  The Windows version is currently mastered on the branch `windows`, which will be merged at a later date.  
  The build instructions for Windows are currently out of date, and will be updated at a later date.__
  * [Issue #113](https://github.com/awslabs/amazon-kinesis-producer/issues/113)
  * [Issue #74](https://github.com/awslabs/amazon-kinesis-producer/issues/74)
  * [Issue #73](https://github.com/awslabs/amazon-kinesis-producer/issues/73)
* Removed the libc wrapper  
  The libc wrapper lowered the required version of glibc.  The KPL is now built with an older version of libc, which removes the need for the wrapper.  
  * [PR #139](https://github.com/awslabs/amazon-kinesis-producer/pull/139)
* Set the minimum required version of macOS to 10.9.  
  The KPL is now built against macOS 10.9.  
  * [Issue #117](https://github.com/awslabs/amazon-kinesis-producer/issues/117)
  * [PR #138](https://github.com/awslabs/amazon-kinesis-producer/pull/138)

#### Java
* Allow exceptions to bubble to the thread exception handler for Daemon threads.  
  Exceptions that occur on daemon threads will now be allowed to propagate to the thread exception handler.  This doesn't provide any additional monitoring or handling of thread death.  
  * [PR #112](https://github.com/awslabs/amazon-kinesis-producer/pull/112)
  * [Issue #111](https://github.com/awslabs/amazon-kinesis-producer/issues/111)
* Updated `amazon-kinesis-producer-sample` to use the correct properties in its configuration file.  
  * [PR #120](https://github.com/awslabs/amazon-kinesis-producer/pull/120)
  * [Issue #119](https://github.com/awslabs/amazon-kinesis-producer/issues/119)
* Updated documentation of `AggregationMaxSize` to match actual Kinesis limits.  
  * [PR #133](https://github.com/awslabs/amazon-kinesis-producer/pull/133)
* Added support for setting `ThreadingModel`, and `ThreadPoolSize` using a properties file.  
  * [PR #134](https://github.com/awslabs/amazon-kinesis-producer/pull/134)
  * [Issue #124](https://github.com/awslabs/amazon-kinesis-producer/issues/124)
* Extracted `IKinesisProducer` from `KinesisProducer` to allow for easier testing.  
  * [PR #136](https://github.com/awslabs/amazon-kinesis-producer/pull/136)

### 0.12.5

#### C++ Core

* Revert to an older version of glibc.
  * [PR #108](https://github.com/awslabs/amazon-kinesis-producer/pull/108)
* Update bootstrap.sh to include new compiler options for the newer version of GCC.
  * [PR #109](https://github.com/awslabs/amazon-kinesis-producer/pull/109)

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
