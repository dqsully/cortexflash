ifeq ($(OS), Windows_NT)
	UNAME := Windows_NT
else
	UNAME := $(shell uname)
endif

ifeq ($(UNAME), Darwin)
	CC = $(CROSS_COMPILE)gcc -arch i386 -arch x86_64  -mmacosx-version-min=10.6
else
	CC = $(CROSS_COMPILE)gcc
endif

AR = $(CROSS_COMPILE)ar
export CC
export AR

all:
	$(CC) -o cortexflash \
		main.c \
		parser.c \
		utils.c \
		stm32.c \
		serial_common.c \
		serial_platform.c \
		stm32/stmreset_binary.c \
		-Wall

clean:
ifeq ($(UNAME), Windows_NT)
	-del /S *.o *.gch
else
	-rm -rf *.o *.gch
endif

install: all
	cp cortexflash /usr/local/bin
