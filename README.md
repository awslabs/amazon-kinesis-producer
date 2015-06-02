# Kinesis Producer Library

## Introduction

The Amazon Kinesis Producer Library (KPL) performs many tasks common to creating efficient and reliable producers for [Amazon Kinesis][amazon-kinesis]. By using the KPL, customers do not need to develop the same logic every time they create a new application for data ingestion.

For detailed information and installation instructions, see the article [Developing Producer Applications for Amazon Kinesis Using the Amazon Kinesis Producer Library][amazon-kpl-docs] in the [Amazon Kinesis Developer Guide][kinesis-developer-guide].

## Supported Platforms and Languages

The KPL is written in C++ and runs as a child process to the main user process. Precompiled native binaries are bundled with the Java release and are managed by the Java wrapper.

On the following operating systems the Java package runs without the need to install any additional libraries:

+ Amazon Linux (2012.09, 2013.03, 2014.09, 2015.03)
+ CentOs 7.0
+ Fedora (2013, 2014, 2015)
+ Gentoo (2014, 2015)
+ OpenSuse 13 (2014)
+ RedHat 7.1
+ SUSE Linux Enterprise Server 12 x86_64
+ Ubuntu Server (13.04, 14.04, 15.04)
+ Apple OS X (10.9, 10.10)

Note the release is 64-bit only.

The release does not contain any GPL licensed code or binaries. The binaries were compiled with LLVM/clang and linked against LLVM's libc++ (which is included).

[kinesis-developer-guide]: http://docs.aws.amazon.com/kinesis/latest/dev/introduction.html
[amazon-kinesis]: http://aws.amazon.com/kinesis
[amazon-kpl-docs]: http://docs.aws.amazon.com/kinesis/latest/dev/developing-producers-with-kpl.html

## Compiling the Native Code

Java developers are encouraged to use the [KPL release in Maven](https://search.maven.org/#search%7Cga%7C1%7Camazon-kinesis-producer), which includes pre-compiled native binaries.

However, on older Linux distributions, it may be necessary to build the native code from source because the release is compiled with a minimum glibc version requirement of 2.17.

Currently only GCC is officially supported for building on Linux. Clang support for Linux will be added later. Advanced users can modify the build scripts themselves to use clang or other compilers.

The project uses [boost build](http://www.boost.org/build/).

### Building on OSX

An upgrade to the latest xcode command line tools is highly recommended.

The code requires very recent libc++ headers. If compilation fails because of non-existant header files in the standard lib, you'll need to update the headers. You can get them from the LLVM sources.

Once you have the repo checked out, run `bootstrap.sh`. This will download and compile all the dependencies. They will be installed into `{project_root}/third_party`. The script automatically downloads a `libc++.dylib` into that directory as well. At the time of writing the OSX system `libc++.dylib` is insufficiently recent.

Run `./b2 -j 8` to build in debug mode.

Run `./b2 release -j 8` to build release.

Unit tests:
```
./b2 release -j 8 && DYLD_LIBRARY_PATH=./third_party/lib ./bin/darwin-4.2.1/release/tests
```

### Building on Linux with GCC

You will need gcc 4.9.x or 5.1.x.

If you have a really old kernel (e.g. 2.6.18), you will most likely need 5.1.0 because gcc 4.9 fails to compile the project when we used RHEL5, potentially due to a [gcc bug](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=65147). You may also need to install or update other uilities needed by gcc 5.1.0 (e.g binutils). 

After that, it's the same as above, i.e.

```
./bootstrap.sh
./b2 -j 8
```

Unit tests:

```
./b2 release -j 16 && LD_LIBRARY_PATH=./third_party/lib:/usr/local/lib64 ./bin/gcc-4.9.2/release/tests --report_level=detailed
```

The above assumes you have the new libstdc++ (that came with the new gcc) installed in `/usr/local/lib64`. If you put it somewhere else, change the `LD_LIBRARY_PATH` as appropriate.

#### Ubuntu 15

This script for Ubuntu 15 installs the necessary tools and performs the build.

```
sudo apt-get update
sudo apt-get install -y g++ automake autoconf git make

git clone https://github.com/awslabs/amazon-kinesis-producer amazon-kinesis-producer

cd amazon-kinesis-producer
./bootstrap.sh
./b2 -j 8
```

### Using the Java Wrapper with the Compiled Native Binaries

There are two options. You can either pack the binaries into the jar like we did for the official release, or you can deploy the native binaries separately and point the java code at it.

#### Packing Native Binaries into Jar

You will need JDK 1.7+, Apache Maven and Python 2.4+ installed.

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

The `Configuration` class provides an option `setNativeExecutable(String val)`. You can use this to provide a path to the `kinesis_producer` executable you have built.

Keep in mind that you need to configure `LD_LIBRARY_PATH` (`DYLD_LIBRARY_PATH` on OSX
) correctly so that all the dependencies can be found. You can set it in the environment when launching the java program; the child process will inherit the environment. Alternatively, you can use a wrapper shell script and have `setNativeExecutable` point to the script instead.

