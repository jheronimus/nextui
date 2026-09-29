ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif

TARGET = keymon
PRODUCT = build/$(PLATFORM)/$(TARGET).elf

CC = $(CROSS_COMPILE)gcc
CFLAGS = -O2 -I../libmsettings -I../platform -I../../all/common -DPLATFORM=\"$(PLATFORM)\"
LDFLAGS = -L../libmsettings -lmsettings -lpthread -lrt

SOURCES = $(TARGET).c

all:
	mkdir -p build/$(PLATFORM)
	$(CC) $(SOURCES) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)
clean:
	rm -rf build $(TARGET).elf
