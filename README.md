# Kinesis Producer Library

## Introduction

The Amazon Kinesis Producer Library (KPL) performs many tasks common to creating efficient and reliable producers for [Amazon Kinesis][amazon-kinesis]. By using the KPL, customers do not need to develop the same logic every time they create a new application for data ingestion.

For detailed information and installation instructions, see the article [Developing Producer Applications for Amazon Kinesis Using the Amazon Kinesis Producer Library][amazon-kpl-docs] in the [Amazon Kinesis Developer Guide][kinesis-developer-guide].

## Release Notes

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
+ For working with China region, set BOTH customEndpoint (e.g: kinesis.cn-north-1.amazonaws.com.cn) and region (e.g: cn-north-1) properties in KinesisProducerConfiguration.

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

Rather than compiling from source, Java developers are encouraged to use the [KPL release in Maven](https://search.maven.org/#search%7Cga%7C1%7Camazon-kinesis-producer), which includes pre-compiled native binaries for Linux, OSX and Windows.

### Building on OSX with Clang

An upgrade to the latest xcode command line tools is recommended.

Once you have the repo checked out, run `bootstrap.sh`. This will download and compile all the dependencies. They will be installed into `{project_root}/third_party`.

Run `./b2 -j 8` to build in debug mode.

Run `./b2 release -j 8` to build release.

Unit tests:

```
./b2 release -j 8 && ./bin/darwin-4.2.1/release/tests --report_level=detailed
```

### Building on Linux with GCC

Currently only GCC is officially supported for building on Linux. Using other compilers may require modifying the build script (the project uses [boost build](http://www.boost.org/build/)).

You will need gcc 4.9.x (or above).

After that, it's the same as above, i.e.

```
./bootstrap.sh
./b2 release -j 8
```

Unit tests:

```
./b2 release -j 8 && ./bin/gcc-4.9.2/release/tests --report_level=detailed
```

Note that if you build on a newer distribution and then deploy to an older one, the program may fail to run because it was linked against a newer version of glibc that the old OS doesn't have. To avoid this problem, build on the same (or a similar) distribution as the one you'll be using in production. We build the official release with RHEL 5 (kernel 2.6.18, glibc 2.5).

### Building on Windows with MinGW

At the time of writing MSVC (14.0) is not able to compile the KPL native code. As such, we have to use MinGW.

We have successfully built and tested the KPL using the nuwen.net MinGW release 13.0. You can get it [here](http://nuwen.net/mingw.html). Get the version that includes git.

Once you have extracted MinGW, go to the MinGW root folder and run `open_distro_window.bat`. This opens a command prompt with the current directory set to the MinGW root folder.

From that command prompt, run `git\git-bash`. This will turn the command prompt into a bash shell.

From there on it's the same procedure as Linux and OSX, i.e.:

```
# You may wish to cd to another dir first
git clone https://github.com/awslabs/amazon-kinesis-producer amazon-kinesis-producer

cd amazon-kinesis-producer
./bootstrap.sh

# We need to specify the toolset, otherwise boost will use MSVC by default, which won't work
./b2 release -j 8 toolset=gcc
```

If you encounter an error that says `File or path name too long`, try moving the project folder to `/c/` to shorten the paths.

Unit tests:

```
./b2 release -j 8 toolset=gcc && bin/gcc-mingw-5.1.0/release/tests.exe --report_level=detailed
```

You may need to increase the command prompt's line and/or buffer size to see the whole test report.

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

