#!/bin/bash

set -e
set -x

INSTALL_DIR=$(pwd)/third_party
mkdir -p $INSTALL_DIR

if [ $1 == "clang" ] || [ $(uname) == 'Darwin' ]; then
  export CC=$(which clang)
  export CXX=$(which clang++)
  export CXXFLAGS="-I$INSTALL_DIR/include -O3 -stdlib=libc++"
  export C_INCLUDE_PATH="$INSTALL_DIR/include"

  if [ $(uname) == 'Linux' ]; then
    export LDFLAGS="-L$INSTALL_DIR/lib -L/usr/local/lib -nodefaultlibs -lpthread -ldl -lc++ -lc++abi -lm -lc -lgcc_s -lgcc"
    export CPLUS_INCLUDE_PATH="/usr/local/include/c++/v1:/usr/include/c++/v1"
    export LD_LIBRARY_PATH="$INSTALL_DIR/lib:/usr/local/lib:$LD_LIBRARY_PATH"
  else
    export LDFLAGS="-L$INSTALL_DIR/lib"
    export DYLD_LIBRARY_PATH="$INSTALL_DIR/lib:$DYLD_LIBRARY_PATH"
  fi
else
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
  (unset LD_LIBRARY_PATH; curl -L $@)
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
  _curl 'https://www.openssl.org/source/openssl-1.0.1m.tar.gz' > openssl.tgz
  tar xf openssl.tgz
  rm openssl.tgz

  cd openssl-1.0.1m

  OPTS="threads shared no-idea no-camellia no-seed no-bf no-cast no-rc2 no-rc4 no-rc5 no-md2 no-md4 no-ripemd no-mdc2 no-ssl2 no-ssl3 no-krb5 no-jpake no-capieng"

  if [[ $(uname) == 'Darwin' ]]; then
    ./Configure darwin64-x86_64-cc $OPTS --prefix=$INSTALL_DIR
  else
    ./config $OPTS --prefix=$INSTALL_DIR
  fi

  make # don't use -j, doesn't work half the time
  make install

  cd ..
fi

# Boost C++ Libraries
if [ ! -d "boost_1_58_0" ]; then
  _curl 'https://s3-us-west-1.amazonaws.com/chaodeng-us-west-1/boost_1_58_0.tar.gz' > boost.tgz
  tar xf boost.tgz
  rm boost.tgz

  cd boost_1_58_0

  LIBS="atomic,chrono,context,system,test,random,regex,thread,coroutine,filesystem"
  OPTS="-j 8 --build-type=minimal --prefix=$INSTALL_DIR link=shared threading=multi install"

  if [[ $(uname) == 'Darwin' ]]; then
    ./bootstrap.sh --with-libraries=$LIBS
    ./b2 toolset=clang-darwin $OPTS
  else
    if [ $1 == "clang" ]; then
      ./bootstrap.sh --with-libraries="$LIBS" --with-toolset=clang
      ./b2 toolset=clang $OPTS cxxflags="$CXXFLAGS" linkflags="$LDFLAGS"
    else
      ./bootstrap.sh --with-libraries="$LIBS" --with-toolset=gcc
      ./b2 toolset=gcc $OPTS
    fi
  fi

  cd ..
fi

# Google Protocol Buffers
if [ ! -d "protobuf-2.6.1" ]; then
  _curl 'https://github.com/google/protobuf/releases/download/v2.6.1/protobuf-2.6.1.tar.gz' > protobuf.tgz
  tar xf protobuf.tgz
  rm protobuf.tgz

  cd protobuf-2.6.1

  conf
  make -j
  make install

  cd ..
fi

# Json codec
if [ ! -d "rapidjson" ]; then
  git clone 'https://github.com/miloyip/rapidjson.git'
  cp -r rapidjson/include/rapidjson include/
fi

# google glog
if [ ! -d "glog-0.3.4" ]; then
  _curl 'https://github.com/google/glog/archive/v0.3.4.tar.gz' > glog.tgz
  tar xf glog.tgz
  rm glog.tgz

  cd glog-0.3.4

  conf
  make -j
  make install

  cd ..
fi

# libunwind
if [ $1 == "clang" ] || [ $(uname) == 'Darwin' ]; then
  echo "Skipping libunwind"
else
  if [ ! -d  "libunwind-1.1" ]; then
    _curl 'http://download.savannah.gnu.org/releases/libunwind/libunwind-1.1.tar.gz' > libunwind.tgz
    tar xf libunwind.tgz
    rm libunwind.tgz

    cd libunwind-1.1

    conf
    make -j
    make install

    cd ..
  fi
fi

# google performance tools
if [ ! -d  "gperftools-2.4" ]; then
  _curl 'https://googledrive.com/host/0B6NtGsLhIcf7MWxMMF9JdTN3UVk/gperftools-2.4.tar.gz' > gperftools-2.4.tar.gz
  tar xf gperftools-2.4.tar.gz
  rm gperftools-2.4.tar.gz

  cd gperftools-2.4

  conf --enable-frame-pointers

  if [ $1 == "clang" ] || [ $(uname) == 'Darwin' ]; then
    $SED 's/#define HAVE_TLS 1//g' src/config.h
  fi

  make -j
  make install

  cd ..
fi

cd ..

if [[ "$OSTYPE" == "darwin"* ]]; then
  _curl 'https://s3-us-west-1.amazonaws.com/chaodeng-us-west-1/libc%2B%2B.dylib' > ./third_party/lib/libc++.1.dylib
  cp ./third_party/lib/libc++.1.dylib ./third_party/lib/libc++.dylib
fi

ln -sf ./third_party/boost_?_*_*/b2 b2

set +e
set +x

echo "***************************************"
echo "Bootstrap complete. Run ./b2 to build."
echo "***************************************"
