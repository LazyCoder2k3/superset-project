
#CROSS_COMPILE=""
#CROSS_COMPILE=/home/mattlin/Desktop/works/acs/Q654/crossgcc/gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
CC=$(CROSS_COMPILE)gcc
CXX=$(CROSS_COMPILE)g++

DEBUG=0

#VIVANTE_SDK_DIR=/home/mattlin/VeriSilicon/VivanteIDE5.8.2/cmdtools/vsimulator
VIVANTE_SDK_DIR=./vip9000sdk-6.4.15.9/6.4.15.9

INCLUDES=-I. -I$(VIVANTE_SDK_DIR)/include/ \
 -I$(VIVANTE_SDK_DIR)/include/CL \
 -I$(VIVANTE_SDK_DIR)/include/VX \
 -I$(VIVANTE_SDK_DIR)/include/ovxlib \
 -I$(VIVANTE_SDK_DIR)/include/ovxlib/utils \
  $(shell pkg-config opencv4 --cflags) \
 -I$(VIVANTE_SDK_DIR)/include/jpeg

CFLAGS=-Wall -std=c++0x $(INCLUDES) -D__linux__ -DLINUX -fpermissive -fopenmp
CFLAGS+=-O3
LFLAGS+=-O3 -Wl,-rpath-link=$(VIVANTE_SDK_DIR)/drivers

LIBS+= -L$(VIVANTE_SDK_DIR)/drivers \
 -lOpenVX -lOpenVXU -lovxlib -ljpeg -lm $(shell pkg-config opencv4 --libs) -lstdc++ -pthread -lX11 -lportaudio -lasound

SRCS=${wildcard *.c}
SRCS+=${wildcard *.cpp}

BIN=superset

OBJS=$(addsuffix .o, $(basename $(SRCS)))

.SUFFIXES: .cpp .c

.cpp.o:
	$(CC) $(CFLAGS) -c $<

.cpp:
	$(CXX) $(CFLAGS) $< -o $@ -lm

.c.o:
	$(CC) $(CFLAGS) -c $<

.c:
	$(CC) $(CFLAGS) $< -o $@ -lm

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LFLAGS) $(EXTRALFLAGS) $(OBJS) $(LIBS) -o $@

clean:
	rm -rf *.o
	rm -rf $(BIN)
	rm -rf *~
