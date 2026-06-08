TARGET = rpc_client
CC = arm-buildroot-linux-gnueabihf-gcc

TOP_DIR = $(shell pwd)/../
LIBEV_DIR = ${TOP_DIR}/libev/tmp/
JSONRPC_DIR = ${TOP_DIR}/jsonrpc-c/tmp/
SHARED_DIR = ${TOP_DIR}/../shared_lib

CFLAGS = -I${LIBEV_DIR}/include -I${JSONRPC_DIR}/include -I${SHARED_DIR}/include
LDFLAGS = ${JSONRPC_DIR}/lib/libjsonrpcc.a ${LIBEV_DIR}/lib/libev.a

# cJSON 从 shared_lib 编译
c_files = rpc_client.c
shared_files = ${SHARED_DIR}/src/cJSON.c

all:
	${CC} ${CFLAGS} -o ${TARGET} ${c_files} ${shared_files} ${LDFLAGS}

clean:
	rm -f *.o ${TARGET}
	
