TARGET = rpc_server
CC = arm-buildroot-linux-gnueabihf-gcc

TOP_DIR = $(shell pwd)/../
LIBEV_DIR = ${TOP_DIR}/libev/tmp/
JSONRPC_DIR = ${TOP_DIR}/jsonrpc-c/tmp/
SHARED_DIR = ${TOP_DIR}/../shared_lib

CFLAGS = -I${LIBEV_DIR}/include -I${JSONRPC_DIR}/include -I${SHARED_DIR}/include
LDFLAGS = ${JSONRPC_DIR}/lib/libjsonrpcc.a ${LIBEV_DIR}/lib/libev.a -lm -lpthread

# 只保留本项目的源文件，cJSON 和 watchdog 从 shared_lib 编译
c_files = rpc_server.c led.c dht11.c hal.c http_server.c web_api.c camera_manager.c
shared_files = ${SHARED_DIR}/src/cJSON.c ${SHARED_DIR}/src/watchdog.c

all:
	${CC} ${CFLAGS} -o ${TARGET} ${c_files} ${shared_files} ${LDFLAGS}

clean:
	rm -f *.o ${TARGET}
	
