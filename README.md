

# Installation Instructions

RootSpy has a few dependencies. Some of these may already be installed on your system and others may be installed using `conda`. Here is a command to create a new, local conda environment and install all of the dependencies there. Omit any from the conda install that you do not need. 

~~~bash
conda create -y --prefix ./cenv curl zeromq protobuf root doxygen
conda activate ./cenv
export CMAKE_PREFIX_PATH=${CONDA_PREFIX}
# setenv CMAKE_PREFIX_PATH ${CONDA_PREFIX}
~~~

Note that the above should automatically set your 'CMAKE_PREFIX_PATH' environment variable when the conda environment is activated. This should allow the RootSpy cmake configuration to find them. 

### Build xmsg-cpp
~~~bash
git clone https://github.com/JeffersonLab/xmsg-cpp
# Need to change C++ standard to at least 17
sed -i 's/CMAKE_CXX_STANDARD 14/CMAKE_CXX_STANDARD 20/' xmsg-cpp/CMakeLists.txt
export xmsg_ROOT=${PWD}/opt
# setenv xmsg_ROOT ${PWD}/opt
cmake -S xmsg-cpp -B xmsg-cpp.build -DCMAKE_INSTALL_PREFIX=${xmsg_ROOT} -DXMSG_BUILD_TESTS=
nice cmake --build xmsg-cpp.build --target install -j32
~~~

## Build cmsg
~~~bash
git clone https://github.com/JeffersonLab/cmsg
export cmsg_ROOT=${PWD}/opt
# setenv cmsg_ROOT ${PWD}/opt
cmake -S cmsg -B cmsg.build -DCODA_INSTALL=${cmsg_ROOT}
nice cmake --build cmsg.build --target install -j32
~~~

## Build RootSpy
~~~bash
git clone https://github.com/JeffersonLab/RootSpy
export RootSpy_ROOT=${PWD}/opt 
# setenv RootSpy_ROOT ${PWD}/opt 
cmake -S RootSpy -B RootSpy.build -DCMAKE_INSTALL_PREFIX=${RootSpy_ROOT}
nice cmake --build RootSpy.build -j32
~~~

<hr>
<hr>

setenv INSTALLDIR /work/epsci/angen/2024.07.02.RootSpy/opt/usr/local

## Setup Root Env

source /cvmfs/oasis.opensciencegrid.org/jlab/scicomp/sw/el9/root/6.30.06-gcc11.4.0/bin/thisroot.csh 
setenv root_ROOT $ROOTSYS
printenv root_ROOT
setenv LD_LIBRARY_PATH ${LD_LIBRARY_PATH}:${INSTALLDIR}/lib:${INSTALLDIR}/lib64


## Build zmq
~~~bash
wget https://github.com/zeromq/libzmq/releases/download/v4.3.5/zeromq-4.3.5.tar.gz
tar xzf zeromq-4.3.5.tar.gz
setenv ZeroMQ_ROOT ${INSTALLDIR}
cmake -S zeromq-4.3.5 -B zeromq-4.3.5.build -DCMAKE_INSTALL_PREFIX=${ZeroMQ_ROOT}
nice cmake --build zeromq-4.3.5.build --target install -j32
~~~

### Build protobuf
~~~bash
git clone --branch v3.10.1 --depth 1 https://github.com/protocolbuffers/protobuf.git protobuf-3.10.1
setenv Protobuf_ROOT ${INSTALLDIR}
cd protobuf-3.10.1
git submodule update --init --recursive --depth 1
./autogen.sh
./configure --prefix ${Protobuf_ROOT}
nice make -j32 install
cd -
~~~

### Build xmsg-cpp
~~~bash
git clone https://github.com/JeffersonLab/xmsg-cpp
setenv xmsg_ROOT ${INSTALLDIR} 
cmake -S xmsg-cpp -B xmsg-cpp.build -DCMAKE_INSTALL_PREFIX=${xmsg_ROOT} -DCMAKE_POLICY_DEFAULT_CMP0074=NEW
nice cmake --build xmsg-cpp.build --target install -j32
~~~

## Build cmsg
~~~bash
git clone https://github.com/JeffersonLab/cmsg
setenv cmsg_ROOT ${INSTALLDIR}  
setenv CODA ${INSTALLDIR}
cmake -S cmsg -B cmsg.build -DCMAKE_INSTALL_PREFIX=${cmsg_ROOT} -DCMAKE_POLICY_DEFAULT_CMP0074=NEW
nice cmake --build cmsg.build --target install -j32
~~~

### CURL
git clone https://github.com/curl/curl.git
setenv CURL_ROOT ${INSTALLDIR}
cmake -S curl -B curl.build -DCMAKE_INSTALL_PREFIX=${CURL_ROOT}
cmake --build curl.build --target install 


# Building RootSpy with cmake

Here is a directory where I built RootSpy with a new cmake configuration,

~~~bash
git clone https://github.com/marieasn/RootSpy
~~~




## Commands to build RootSpy

Here are some instructions for building RootSpy, including its dependencies xmsg-cpp,
zmq, and protobuf.

Protobuf is quirky in that we use an older version that uses libtool which complains
if it is installing in a directory that doesn't end with /usr/local/lib. Thus, we
install everything in ${PWD}/opt/usr/local.







###
cmake -S RootSpy/src/libRootSpy -B  libRootSpy.build
cmake --build libRootSpy.build

###
cmake -S RootSpy/src/libRootSpy-client -B  libRootSpyclient.build
cmake --build libRootSpyclient.build





