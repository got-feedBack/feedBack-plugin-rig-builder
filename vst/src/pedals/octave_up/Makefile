#!/usr/bin/make -f

NAME = OctaveUp

FILES_DSP = \
	OctaveUpPlugin.cpp

FILES_UI = \
	OctaveUp_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
