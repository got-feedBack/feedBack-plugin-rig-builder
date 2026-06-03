#!/usr/bin/make -f

NAME = BassEnbig

FILES_DSP = \
	BassEnbigPlugin.cpp

FILES_UI = \
	BassEnbig_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
