TARGET=rpc_server
CC=arm-buildroot-linux-gnueabihf-gcc

TOP_DIR=$(shell pwd)/../
LIBEV_DIR=${TOP_DIR}/libev/tmp/
JSONRPC_DIR=${TOP_DIR}/jsonrpc-c/tmp/

CFLAGS=-I${LIBEV_DIR}/include -I${JSONRPC_DIR}/include
LDFLAGS=${JSONRPC_DIR}/lib/libjsonrpcc.a  ${LIBEV_DIR}/lib/libev.a -lm -lpthread

c_files = cJSON.c rpc_server.c led.c dht11.c hal.c watchdog.c http_server.c web_api.c

all:
	${CC} ${CFLAGS} -o ${TARGET} ${c_files} ${LDFLAGS}

clean:
	rm -f *.o ${TARGET}
	
