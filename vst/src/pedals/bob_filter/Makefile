#!/usr/bin/make -f

NAME = BobFilter

FILES_DSP = \
	BobFilterPlugin.cpp

FILES_UI = \
	BobFilter_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
