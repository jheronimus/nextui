###########################################################

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

ifeq (,$(PLATFORM))
$(error please specify PLATFORM, eg. PLATFORM=alpine make)
endif

###########################################################

all: readmes

early:

clean:

include ../all/readmes/makefile
