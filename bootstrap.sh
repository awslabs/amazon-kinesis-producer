#!/bin/bash

set -e
set -x

LIB_MIRROR="http://d3mddf4lzrx5uw.cloudfront.net"

INSTALL_DIR=$(pwd)/third_party
mkdir -p $INSTALL_DIR

if [ $1 == "clang" ] || [ $(uname) == 'Darwin' ]; then
  export CC=$(which clang)
  export CXX=$(which clang++)
  export CXXFLAGS="-I$INSTALL_DIR/include -O3 -stdlib=libc++"
  export C_INCLUDE_PATH="$INSTALL_DIR/include"

  if [ $(uname) == 'Linux' ]; then
    export LDFLAGS="-L$INSTALL_DIR/lib -L/usr/local/lib -nodefaultlibs -lpthread -ldl -lc++ -lc++abi -lm -lc -lgcc_s"
    export CPLUS_INCLUDE_PATH="/usr/local/include/c++/v1:/usr/include/c++/v1"
    export LD_LIBRARY_PATH="$INSTALL_DIR/lib:/usr/local/lib:$LD_LIBRARY_PATH"
  else
    export LDFLAGS="-L$INSTALL_DIR/lib"
    export DYLD_LIBRARY_PATH="$INSTALL_DIR/lib:$DYLD_LIBRARY_PATH"
  fi
else
  export CC="gcc"
  export CXX="g++"
  export CXXFLAGS="-I$INSTALL_DIR/include -O3"
  export LDFLAGS="-L$INSTALL_DIR/lib -L/usr/local/lib64/"
  export LD_LIBRARY_PATH="$INSTALL_DIR/lib:/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH"
fi

SED="sed -i"
if [[ "$OSTYPE" == "darwin"* ]]; then
  SED="sed -i ''"
fi

# Need to unset LD_LIBRARY_PATH for curl because the OpenSSL we build doesn't
# have MD4, which curl tries to use.
function _curl {
  #(unset LD_LIBRARY_PATH; curl -L $@)
  curl -L $@
}

cd $INSTALL_DIR

function conf {
  if [[ "$OSTYPE" == "darwin"* ]]; then
     ./configure \
    --prefix="$INSTALL_DIR" \
    DYLD_LIBRARY_PATH="$DYLD_LIBRARY_PATH" \
    LDFLAGS="$LDFLAGS" \
    CXXFLAGS="$CXXFLAGS" \
    $@
  else
    ./configure \
    --prefix="$INSTALL_DIR" \
    LD_LIBRARY_PATH="$LD_LIBRARY_PATH" \
    LDFLAGS="$LDFLAGS" \
    CXXFLAGS="$CXXFLAGS" \
    C_INCLUDE_PATH="$C_INCLUDE_PATH" \
    CPLUS_INCLUDE_PATH="$CPLUS_INCLUDE_PATH" \
    $@
  fi
}

# OpenSSL
if [ ! -d "openssl-1.0.1m" ]; then
  _curl "$LIB_MIRROR/openssl-1.0.1m.tar.gz" > openssl.tgz
  tar xf openssl.tgz
  rm openssl.tgz

  cd openssl-1.0.1m

  # Have to leave MD4 enabled because curl expects it
  OPTS="threads no-shared no-idea no-camellia no-seed no-bf no-cast no-rc2 no-rc4 no-rc5 no-md2 no-ripemd no-mdc2 no-ssl2 no-ssl3 no-krb5 no-jpake no-capieng"

  if [[ $(uname) == 'Darwin' ]]; then
    ./Configure darwin64-x86_64-cc $OPTS --prefix=$INSTALL_DIR
  elif [[ $(uname) == MINGW* ]]; then
    ./Configure mingw64 $OPTS --prefix=$INSTALL_DIR
    find ./ -name Makefile | while read f; do echo >> $f; echo "%.o: %.c" >> $f; echo -e '\t$(COMPILE.c) $(OUTPUT_OPTION) $<;' >> $f; done
  else
    ./config $OPTS --prefix=$INSTALL_DIR
  fi

  make # don't use -j, doesn't work half the time
  make install

  cd ..
fi

# Boost C++ Libraries
if [ ! -d "boost_1_58_0" ]; then
  _curl "$LIB_MIRROR/boost_1_58_0.tar.gz" > boost.tgz
  tar xf boost.tgz
  rm boost.tgz

  cd boost_1_58_0

  LIBS="atomic,chrono,log,system,test,random,regex,thread,filesystem"
  OPTS="-j 8 --build-type=minimal --layout=system --prefix=$INSTALL_DIR link=static threading=multi release install"

  if [[ $(uname) == 'Darwin' ]]; then
    ./bootstrap.sh --with-libraries=$LIBS
    ./b2 toolset=clang-darwin $OPTS
  elif [[ $(uname) == MINGW* ]]; then
    ./bootstrap.sh --with-libraries=$LIBS --with-toolset=mingw
    sed -i 's/\bmingw\b/gcc/' project-config.jam
    ./b2 $OPTS
  else
    if [ "$1" == "clang" ]; then
      ./bootstrap.sh --with-libraries="$LIBS" --with-toolset=clang
      ./b2 toolset=clang $OPTS cxxflags="$CXXFLAGS" linkflags="$LDFLAGS"
    else
      ./bootstrap.sh --with-libraries="$LIBS" --with-toolset=gcc
      ./b2 toolset=gcc $OPTS
    fi
  fi

  cd ..
fi

# zlib
if [ ! -d "zlib-1.2.8" ]; then
  _curl "$LIB_MIRROR/zlib-1.2.8.tar.gz" > zlib.tgz
  tar xf zlib.tgz
  rm zlib.tgz

  cd zlib-1.2.8

  ./configure --static --prefix="$INSTALL_DIR"
  make -j
  make install

  cd ..
fi


# Google Protocol Buffers
if [ ! -d "protobuf-2.6.1" ]; then
  _curl "$LIB_MIRROR/protobuf-2.6.1.tar.gz" > protobuf.tgz
  tar xf protobuf.tgz
  rm protobuf.tgz

  cd protobuf-2.6.1

  conf --enable-shared=no
  make -j
  make install

  cd ..
fi


# libcurl
if [ ! -d "curl-7.47.0" ]; then
  _curl "$LIB_MIRROR/curl-7.47.0.tar.gz" > curl.tgz
  tar xf curl.tgz
  rm curl.tgz

  cd curl-7.47.0

  conf --disable-shared --disable-ldap --disable-ldaps \
    --enable-threaded-resolver --disable-debug --without-libssh2
  make -j
  make install

  cd ..
fi

# AWS C++ SDK
if [ ! -d "aws-sdk-cpp" ]; then
  git clone https://github.com/awslabs/aws-sdk-cpp.git aws-sdk-cpp
  pushd aws-sdk-cpp
  git checkout 1.0.5
  popd

  rm -rf aws-sdk-cpp-build
  mkdir aws-sdk-cpp-build

  cd aws-sdk-cpp-build

  cmake \
    -DBUILD_ONLY="kinesis;monitoring" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DSTATIC_LINKING=1 \
    -DCMAKE_PREFIX_PATH="$INSTALL_DIR" \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DENABLE_TESTING="OFF" \
    ../aws-sdk-cpp
  make -j 4
  make install

  cd ..

  for f in $(find $INSTALL_DIR -name "libaws-cpp-sdk*.a"); do
    mv $f "$INSTALL_DIR/lib/"
  done
fi

cd ..

ln -sf ./third_party/boost_?_*_*/b2* b2

set +e
set +x

echo "***************************************"
echo "Bootstrap complete. Run ./b2 to build."
echo "***************************************"
