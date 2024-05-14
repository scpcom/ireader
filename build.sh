#!/bin/bash

RELEASE=0

PLATFORM=/home/owen/MaixCDK/dl/extracted/toolchains/maixcam/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl

CURR_DIR=$(cd "$(dirname "$0")";pwd)
INSTALL=${CURR_DIR}
INSTALL_LIB=${INSTALL}/lib
INSTALL_INC=${INSTALL}/include

cd sdk
if [ -z "${PLATFORM}" ]; then
make RELEASE=${RELEASE} clean && make RELEASE=${RELEASE}
else
make RELEASE=${RELEASE} PLATFORM=${PLATFORM} clean && make RELEASE=${RELEASE} PLATFORM=${PLATFORM}
fi
cd -

cd avcodec
if [ -z "${PLATFORM}" ]; then
make RELEASE=${RELEASE} clean && make RELEASE=${RELEASE}
else
make RELEASE=${RELEASE} PLATFORM=${PLATFORM} clean && make RELEASE=${RELEASE} PLATFORM=${PLATFORM}
fi
cd -

cd media-server
if [ -z "${PLATFORM}" ]; then
make RELEASE=${RELEASE} clean && make RELEASE=${RELEASE}
else
make RELEASE=${RELEASE} PLATFORM=${PLATFORM} clean && make RELEASE=${RELEASE} PLATFORM=${PLATFORM}
fi
cd -

if [ ! -d ${INSTALL_LIB} ]; then
    mkdir ${INSTALL_LIB}
else
    rm ${INSTALL_LIB} -rf
    mkdir ${INSTALL_LIB}
fi

if [ ${RELEASE} -eq 1 ]; then
    find ${CURR_DIR}/media-server -type f -path "*/release.${PLATFORM}/*.a" -exec cp {} ${INSTALL_LIB} \;
    find ${CURR_DIR}/media-server -type f -path "*/release.${PLATFORM}/*.so" -exec cp {} ${INSTALL_LIB} \;
    find ${CURR_DIR}/sdk -type f -path "*/release.${PLATFORM}/*.a" -exec cp {} ${INSTALL_LIB} \;
    find ${CURR_DIR}/sdk -type f -path "*/release.${PLATFORM}/*.so" -exec cp {} ${INSTALL_LIB} \;
    find ${CURR_DIR}/avcodec -type f -path "*/release.${PLATFORM}/*.a" -exec cp {} ${INSTALL_LIB} \;
    find ${CURR_DIR}/avcodec -type f -path "*/release.${PLATFORM}/*.so" -exec cp {} ${INSTALL_LIB} \;
else
    find ${CURR_DIR}/media-server -type f -path "*/debug.${PLATFORM}/*.a" -exec cp {} ${INSTALL_LIB} \;
    find ${CURR_DIR}/media-server -type f -path "*/debug.${PLATFORM}/*.so" -exec cp {} ${INSTALL_LIB} \;
    find ${CURR_DIR}/sdk -type f -path "*/debug.${PLATFORM}/*.a" -exec cp {} ${INSTALL_LIB} \;
    find ${CURR_DIR}/sdk -type f -path "*/debug.${PLATFORM}/*.so" -exec cp {} ${INSTALL_LIB} \;
    find ${CURR_DIR}/avcodec -type f -path "*/debug.${PLATFORM}/*.a" -exec cp {} ${INSTALL_LIB} \;
    find ${CURR_DIR}/avcodec -type f -path "*/debug.${PLATFORM}/*.so" -exec cp {} ${INSTALL_LIB} \;
fi

if [ ! -d ${INSTALL_INC} ]; then
    mkdir ${INSTALL_INC}
    mkdir ${INSTALL_INC}/media-server
    mkdir ${INSTALL_INC}/media-server/libdash
    mkdir ${INSTALL_INC}/media-server/libflv
    mkdir ${INSTALL_INC}/media-server/libhls
    mkdir ${INSTALL_INC}/media-server/libmkv
    mkdir ${INSTALL_INC}/media-server/libmov
    mkdir ${INSTALL_INC}/media-server/libmpeg
    mkdir ${INSTALL_INC}/media-server/librtmp
    mkdir ${INSTALL_INC}/media-server/librtp
    mkdir ${INSTALL_INC}/media-server/librtsp
    mkdir ${INSTALL_INC}/media-server/libsip
    mkdir ${INSTALL_INC}/sdk
    mkdir ${INSTALL_INC}/sdk/include
    mkdir ${INSTALL_INC}/sdk/libaio
    mkdir ${INSTALL_INC}/sdk/libhttp
    mkdir ${INSTALL_INC}/sdk/libice
    mkdir ${INSTALL_INC}/avcodec
    mkdir ${INSTALL_INC}/avcodec/avbsf
    mkdir ${INSTALL_INC}/avcodec/avcodec
    mkdir ${INSTALL_INC}/avcodec/h264
    mkdir ${INSTALL_INC}/avcodec/h265
else
    rm ${INSTALL_INC} -rf
    mkdir ${INSTALL_INC}
    mkdir ${INSTALL_INC}/media-server
    mkdir ${INSTALL_INC}/media-server/libdash
    mkdir ${INSTALL_INC}/media-server/libflv
    mkdir ${INSTALL_INC}/media-server/libhls
    mkdir ${INSTALL_INC}/media-server/libmkv
    mkdir ${INSTALL_INC}/media-server/libmov
    mkdir ${INSTALL_INC}/media-server/libmpeg
    mkdir ${INSTALL_INC}/media-server/librtmp
    mkdir ${INSTALL_INC}/media-server/librtp
    mkdir ${INSTALL_INC}/media-server/librtsp
    mkdir ${INSTALL_INC}/media-server/libsip
    mkdir ${INSTALL_INC}/sdk
    mkdir ${INSTALL_INC}/sdk/include
    mkdir ${INSTALL_INC}/sdk/libaio
    mkdir ${INSTALL_INC}/sdk/libhttp
    mkdir ${INSTALL_INC}/sdk/libice
    mkdir ${INSTALL_INC}/avcodec
    mkdir ${INSTALL_INC}/avcodec/avbsf
    mkdir ${INSTALL_INC}/avcodec/avcodec
    mkdir ${INSTALL_INC}/avcodec/h264
    mkdir ${INSTALL_INC}/avcodec/h265
fi

cp -r ${CURR_DIR}/media-server/libdash/include ${INSTALL_INC}/media-server/libdash
cp -r ${CURR_DIR}/media-server/libflv/include ${INSTALL_INC}/media-server/libflv
cp -r ${CURR_DIR}/media-server/libhls/include ${INSTALL_INC}/media-server/libhls
cp -r ${CURR_DIR}/media-server/libmkv/include ${INSTALL_INC}/media-server/libmkv
cp -r ${CURR_DIR}/media-server/libmov/include ${INSTALL_INC}/media-server/libmov
cp -r ${CURR_DIR}/media-server/libmpeg/include ${INSTALL_INC}/media-server/libmpeg
cp -r ${CURR_DIR}/media-server/librtmp/include ${INSTALL_INC}/media-server/librtmp
cp -r ${CURR_DIR}/media-server/librtp/include ${INSTALL_INC}/media-server/librtp
cp -r ${CURR_DIR}/media-server/librtsp/include ${INSTALL_INC}/media-server/librtsp
cp -r ${CURR_DIR}/media-server/libsip/include ${INSTALL_INC}/media-server/libsip
cp -r ${CURR_DIR}/sdk/include ${INSTALL_INC}/sdk
cp -r ${CURR_DIR}/sdk/libaio/include ${INSTALL_INC}/sdk/libaio
cp -r ${CURR_DIR}/sdk/libhttp/include ${INSTALL_INC}/sdk/libhttp
cp -r ${CURR_DIR}/sdk/libice/include ${INSTALL_INC}/sdk/libice
cp -r ${CURR_DIR}/avcodec/avbsf/include ${INSTALL_INC}/avcodec/avbsf
cp -r ${CURR_DIR}/avcodec/avcodec/include ${INSTALL_INC}/avcodec/avcodec
cp -r ${CURR_DIR}/avcodec/h264/include ${INSTALL_INC}/avcodec/h264
cp -r ${CURR_DIR}/avcodec/h265/include ${INSTALL_INC}/avcodec/h265



