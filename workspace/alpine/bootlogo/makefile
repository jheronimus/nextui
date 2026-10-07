ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif

PLATFORM ?= alpine
include ../platform/makefile.env

TARGET = bootlogo
PRODUCT = build/$(PLATFORM)/$(TARGET).elf

CC = $(CROSS_COMPILE)gcc
INCDIR = -I. -I../../all/common -I../platform -I../libmsettings
CFLAGS += $(OPT) $(INCDIR) -DPLATFORM=\"$(PLATFORM)\" -std=gnu99
LDFLAGS += -L../libmsettings -lmsettings

SOURCES = $(TARGET).c ../../all/common/utils.c ../../all/common/api.c ../../all/common/config.c ../../all/common/scaler.c ../platform/platform.c

all: $(PREFIX_LOCAL)/include/msettings.h
	mkdir -p build/$(PLATFORM)
	$(CC) $(SOURCES) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf build $(PRODUCT)

$(PREFIX_LOCAL)/include/msettings.h:
	cd ../libmsettings && make
