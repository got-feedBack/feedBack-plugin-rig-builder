#!/usr/bin/make -f

NAME = OmniMod

FILES_DSP = \
	OmniModPlugin.cpp

FILES_UI = \
	OmniMod_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
