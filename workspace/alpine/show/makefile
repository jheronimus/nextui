ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif

TARGET = show
PRODUCT = $(TARGET).elf

CC = $(CROSS_COMPILE)gcc
CFLAGS = -O2 -I../platform -I../../all/common -DPLATFORM=\"$(PLATFORM)\"
FLAGS	= -Os -lSDL2 -lSDL2_image -lrt -ldl -Wl,--gc-sections -s

SOURCES = $(TARGET).c ../platform/traits.c ../../all/common/utils.c

all:
	$(CC) $(SOURCES) -o $(PRODUCT) $(CFLAGS) $(FLAGS)
clean:
	rm -rf $(PRODUCT)
