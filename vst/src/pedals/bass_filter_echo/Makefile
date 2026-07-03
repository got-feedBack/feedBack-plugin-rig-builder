#!/usr/bin/make -f

NAME = BassFilterEcho

FILES_DSP = \
	BassFilterEchoPlugin.cpp

FILES_UI = \
	BassFilterEcho_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
