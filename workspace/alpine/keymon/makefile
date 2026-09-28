ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif

TARGET = keymon
PRODUCT = $(TARGET).elf

CC = $(CROSS_COMPILE)gcc
CFLAGS = -O2 -I../libmsettings -I../platform -I../../all/common -DPLATFORM=\"$(PLATFORM)\"
LDFLAGS = -L../libmsettings -lmsettings -lpthread -lrt

SOURCES = $(TARGET).c

all:
	$(CC) $(SOURCES) -o $(PRODUCT) $(CFLAGS) $(LDFLAGS)
clean:
	rm -rf $(PRODUCT)
