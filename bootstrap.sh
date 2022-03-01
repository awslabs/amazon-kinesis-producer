#!/bin/bash

set -e
set -x
SILENT="y"

silence() {
    if [ -n "$SILENT" ]; then
        "$@" > /dev/null
    else
        "$@"
    fi
}

LIB_OPENSSL="https://ftp.openssl.org/source/old/1.0.2/openssl-1.0.2u.tar.gz"
LIB_BOOST="http://sourceforge.net/projects/boost/files/boost/1.76.0/boost_1_76_0.tar.gz"
LIB_ZLIB="https://zlib.net/fossils/zlib-1.2.11.tar.gz"
LIB_PROTOBUF="https://github.com/protocolbuffers/protobuf/releases/download/v3.11.4/protobuf-all-3.11.4.tar.gz"
LIB_CURL="https://curl.haxx.se/download/curl-7.81.0.tar.gz"
CA_CERT="https://curl.haxx.se/ca/cacert.pem"


INSTALL_DIR=$(pwd)/third_party
#Cleanup any earlier version of the third party directory and links to it.
rm -f b2
rm -rf $INSTALL_DIR
mkdir -p $INSTALL_DIR

#Figure out the release type from os. The release type will be used to determine the final storage location
# of the native binary
function find_release_type() {
  if [[ $OSTYPE == "linux-gnu" ]]; then
		echo "linux-$(uname -m)"
		return
	elif [[ $OSTYPE == darwin* ]]; then
		echo "osx"
		return
	elif [[ $OSTYPE == "msys" ]]; then
		echo "windows"
		return
	fi

	echo "unknown"
}

CMAKE=$(which cmake3 &> /dev/null && echo "cmake3 " || echo "cmake")
RELEASE_TYPE=$(find_release_type)

[[ $RELEASE_TYPE == "unknown" ]] && {
	echo "Could not define release type for $OSTYPE"
	exit 1
}


if [ $1 == "clang" ] || [ $(uname) == 'Darwin' ]; then
  export MACOSX_DEPLOYMENT_TARGET='10.13'
  export MACOSX_MIN_COMPILER_OPT="-mmacosx-version-min=${MACOSX_DEPLOYMENT_TARGET}"
  export CC=$(which clang)
  export CXX=$(which clang++)
  export CXXFLAGS="-I$INSTALL_DIR/include -O3 -stdlib=libc++ ${MACOSX_MIN_COMPILER_OPT} "
  export CFLAGS="${MACOSX_MIN_COMPILER_OPT} "
  export C_INCLUDE_PATH="$INSTALL_DIR/include"

  if [ $(uname) == 'Linux' ]; then
    export LDFLAGS="-L$INSTALL_DIR/lib -nodefaultlibs -lpthread -ldl -lc++ -lc++abi -lm -lc -lgcc_s"
    export LD_LIBRARY_PATH="$INSTALL_DIR/lib:$LD_LIBRARY_PATH"
  else
    export LDFLAGS="-L$INSTALL_DIR/lib"
    export DYLD_LIBRARY_PATH="$INSTALL_DIR/lib:$DYLD_LIBRARY_PATH"
  fi
else
  export CC="gcc"
  export CXX="g++"
  export CXXFLAGS="-I$INSTALL_DIR/include -O3  -Wno-implicit-fallthrough -Wno-int-in-bool-context"
  export LDFLAGS="-L$INSTALL_DIR/lib "
  export LD_LIBRARY_PATH="$INSTALL_DIR/lib:$LD_LIBRARY_PATH"
fi

SED="sed -i"
if [[ "$OSTYPE" == "darwin"* ]]; then
  SED="sed -i ''"
fi

# Need to unset LD_LIBRARY_PATH for curl because the OpenSSL we build doesn't
# have MD4, which curl tries to use.
function _curl {
  #(unset LD_LIBRARY_PATH; curl -L $@)
  curl -L --cacert "$INSTALL_DIR/cacert.pem" $@
}

cd $INSTALL_DIR
wget --no-check-certificate -P $INSTALL_DIR $CA_CERT

function conf {
  if [[ "$OSTYPE" == "darwin"* ]]; then
    silence ./configure \
    --prefix="$INSTALL_DIR" \
    DYLD_LIBRARY_PATH="$DYLD_LIBRARY_PATH" \
    LDFLAGS="$LDFLAGS" \
    CXXFLAGS="$CXXFLAGS" \
    $@
  else
    silence ./configure \
    --prefix="$INSTALL_DIR" \
    LD_LIBRARY_PATH="$LD_LIBRARY_PATH" \
    LDFLAGS="$LDFLAGS" \
    CXXFLAGS="$CXXFLAGS" \
    C_INCLUDE_PATH="$C_INCLUDE_PATH" \
    $@
  fi
}

# OpenSSL
if [ ! -d "openssl-1.0.2u" ]; then
  _curl "$LIB_OPENSSL" > openssl.tgz
  tar xf openssl.tgz
  rm openssl.tgz

  cd openssl-1.0.2u

  # Have to leave MD4 enabled because curl expects it
  OPTS="threads no-shared no-idea no-camellia no-seed no-bf no-cast no-rc2 no-rc4 no-rc5 no-md2 no-ripemd no-mdc2 no-ssl2 no-ssl3 no-krb5 no-jpake no-capieng no-dso"

  if [[ $(uname) == 'Darwin' ]]; then
    silence ./Configure darwin64-x86_64-cc $OPTS --prefix=$INSTALL_DIR
  elif [[ $(uname) == MINGW* ]]; then
    silence ./Configure mingw64 $OPTS --prefix=$INSTALL_DIR
    find ./ -name Makefile | while read f; do echo >> $f; echo "%.o: %.c" >> $f; echo -e '\t$(COMPILE.c) $(OUTPUT_OPTION) $<;' >> $f; done
  else
    silence ./config $OPTS --prefix=$INSTALL_DIR
  fi

  silence make depend
  silence make # don't use -j, doesn't work half the time
  silence make install

  cd ..
fi

# Boost C++ Libraries
if [ ! -d "boost_1_76_0" ]; then
  _curl "$LIB_BOOST" > boost.tgz
  tar xf boost.tgz
  rm boost.tgz

  cd boost_1_76_0

  LIBS="atomic,chrono,log,system,test,random,regex,thread,filesystem"
  OPTS="-j 8 --build-type=minimal --layout=system --prefix=$INSTALL_DIR link=static threading=multi release install"

  if [[ $(uname) == 'Darwin' ]]; then
    silence ./bootstrap.sh --with-libraries=$LIBS
    silence ./b2 toolset=clang-darwin $OPTS cxxflags="$MACOSX_MIN_COMPILER_OPT"
  elif [[ $(uname) == MINGW* ]]; then
    silence ./bootstrap.sh --with-libraries=$LIBS --with-toolset=mingw
    sed -i 's/\bmingw\b/gcc/' project-config.jam
    silence ./b2 $OPTS
  else
    if [ "$1" == "clang" ]; then
      silence ./bootstrap.sh --with-libraries="$LIBS" --with-toolset=clang
      silence ./b2 toolset=clang $OPTS cxxflags="$CXXFLAGS" linkflags="$LDFLAGS"
    else
      silence ./bootstrap.sh --with-libraries="$LIBS" --with-toolset=gcc
      silence ./b2 toolset=gcc $OPTS
    fi
  fi

  cd ..
fi

# zlib
if [ ! -d "zlib-1.2.11" ]; then
  _curl "$LIB_ZLIB" > zlib.tgz
  tar xf zlib.tgz
  rm zlib.tgz

  cd zlib-1.2.11
  silence ./configure --static --prefix="$INSTALL_DIR"
  silence make -j
  silence make install

  cd ..
fi

# Google Protocol Buffers
if [ ! -d "protobuf-3.11.4" ]; then
  _curl "$LIB_PROTOBUF" > protobuf.tgz
  tar xf protobuf.tgz
  rm protobuf.tgz

  cd protobuf-3.11.4
  silence conf --enable-shared=no
  silence make -j 4
  silence make install

  cd ..
fi


# libcurl
if [ ! -d "curl-7.81.0" ]; then
  _curl "$LIB_CURL" > curl.tgz
  tar xf curl.tgz
  rm curl.tgz


  cd curl-7.81.0

  silence conf --disable-shared --disable-ldap --disable-ldaps --without-libidn2 \
       --enable-threaded-resolver --disable-debug --without-libssh2 --without-ca-bundle --with-ssl="${INSTALL_DIR}" --without-libidn
  if [[ $(uname) == 'Darwin' ]]; then
    #
    # Apply a patch for macOS that should prevent curl from trying to use clock_gettime
    # This is a temporary work around for https://github.com/awslabs/amazon-kinesis-producer/issues/117
    # until dependencies are updated
    #
    sed -Ei .bak 's/#define HAVE_CLOCK_GETTIME_MONOTONIC 1//' lib/curl_config.h
  fi
  silence make -j
  silence make install

  cd ..
fi

# AWS C++ SDK
if [ ! -d "aws-sdk-cpp" ]; then
  git clone https://github.com/awslabs/aws-sdk-cpp.git aws-sdk-cpp
  pushd aws-sdk-cpp
  git checkout 1.8.30
  popd

  rm -rf aws-sdk-cpp-build
  mkdir aws-sdk-cpp-build

  cd aws-sdk-cpp-build

  silence $CMAKE \
    -DBUILD_ONLY="kinesis;monitoring" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DSTATIC_LINKING=1 \
    -DCMAKE_PREFIX_PATH="$INSTALL_DIR" \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_FIND_FRAMEWORK=LAST \
    -DENABLE_TESTING="OFF" \
    ../aws-sdk-cpp
  silence make -j 4
  silence make install

  cd ..

fi

cd ..

#Build the native kinesis producer
$CMAKE -DCMAKE_PREFIX_PATH="$INSTALL_DIR" .
make -j8

#copy native producer to a location that the java producer can package it
NATIVE_BINARY_DIR=java/amazon-kinesis-producer/src/main/resources/amazon-kinesis-producer-native-binaries/$RELEASE_TYPE/
mkdir -p $NATIVE_BINARY_DIR
cp kinesis_producer $NATIVE_BINARY_DIR


#build the java producer and install it locally
pushd java/amazon-kinesis-producer
mvn clean package source:jar javadoc:jar install
popd


set +e
set +x
