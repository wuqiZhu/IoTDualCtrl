TARGET = rpc_client
CC = arm-buildroot-linux-gnueabihf-gcc

SHARED_DIR = $(shell pwd)/../../shared_lib
CFLAGS = -I${SHARED_DIR}/include
LDFLAGS = -lm

c_files = rpc_client.c
shared_files = ${SHARED_DIR}/src/cJSON.c

all:
	${CC} ${CFLAGS} -o ${TARGET} ${c_files} ${shared_files} ${LDFLAGS}

clean:
	rm -f *.o ${TARGET}
	
