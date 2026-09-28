ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif
ifeq (,$(PREFIX))
$(error missing PREFIX for this toolchain)
endif

TARGET=msettings

.PHONY: build clean

CC = $(CROSS_COMPILE)gcc

SYSROOT := $(shell $(CC) --print-sysroot)
INCLUDEDIR = $(SYSROOT)/usr/include
CFLAGS = -I$(INCLUDEDIR) -I../platform -I../../all/common -DPLATFORM=\"$(PLATFORM)\"
LDFLAGS = -ltinyalsa -ldl -lrt -lm -s
OPTM = -Ofast

SOURCES = $(TARGET).c ../platform/traits.c ../../all/common/utils.c ../../all/common/displaycal.c
OBJECTS = $(TARGET).o traits.o utils.o displaycal.o

build:
	mkdir -p "$(PREFIX)/include" "$(PREFIX)/lib"
	$(CC) -c -Werror -fpic $(OPTM) $(CFLAGS) $(SOURCES)
	$(CC) -shared -o "lib$(TARGET).so" $(OBJECTS) $(LDFLAGS)
	cp "$(TARGET).h" "$(PREFIX)/include"
	cp "lib$(TARGET).so" "$(PREFIX)/lib"
	@if [ -n "$(PREFIX_LOCAL)" ]; then \
		mkdir -p "$(PREFIX_LOCAL)/include" "$(PREFIX_LOCAL)/lib"; \
		cp "$(TARGET).h" "$(PREFIX_LOCAL)/include/"; \
		cp "lib$(TARGET).so" "$(PREFIX_LOCAL)/lib/"; \
	fi

clean:
	rm -f *.o
	rm -f "lib$(TARGET).so"
	rm -f $(PREFIX)/include/$(TARGET).h
	rm -f $(PREFIX)/lib/lib$(TARGET).so
