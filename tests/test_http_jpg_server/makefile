# CROSS_COMPILE ?= /home/owen/MaixCDK/dl/extracted/toolchains/maixcam/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-
# CC := $(CROSS_COMPILE)gcc
# LD := $(CROSS_COMPILE)ld

CFLAGS = -Wall -O -g -DOS_LINUX
SRCS = $(wildcard *.c)
OBJS = $(patsubst %c, %o, $(SRCS))

ROOT = $(realpath ../..)

SDK = ${ROOT}/include/sdk
AVCODEC = ${ROOT}/include/avcodec
SERVER = ${ROOT}/include/media-server

INCLUDES = 	-I. \
			-I$(SDK)/include \
			-I$(SDK)/libaio/include \
			-I$(SDK)/libhttp/include \
			-I$(SDK)/libice/include \
			-I$(AVCODEC)/avbsf/include \
			-I$(AVCODEC)/avcodec/include \
			-I${SERVER}/libdash/include \
			-I${SERVER}/libflv/include \
			-I${SERVER}/libhls/include \
			-I${SERVER}/libmov/include \
			-I${SERVER}/libmpeg/include \
			-I${SERVER}/libmkv/include \
			-I${SERVER}/librtmp/include \
			-I${SERVER}/librtmp/aio \
			-I${SERVER}/librtp/include \
			-I${SERVER}/librtsp/include \
			-I${SERVER}/libsip/include
CFLAGS += ${INCLUDES}

LIBS = -lhttp -lice -lsdk  -lavbsf -lavcodec -lh264 -lh265 -lhls -lmkv -lmov -lmpeg -lrtmp -lflv -lrtp -lrtsp -lsip -laio
LDFLAGS = ${LIBS} -L${ROOT}/lib -lstdc++

TARGET = test

.PHONY:all clean
$(TARGET): $(OBJS)
	@$(CC) -o $@ $^ ${CFLAGS} ${LDFLAGS}

%.o:%.c
	@$(CC) -c $^ ${CFLAGS} ${LDFLAGS}

clean:
	rm -f $(OBJS) $(TARGET)
