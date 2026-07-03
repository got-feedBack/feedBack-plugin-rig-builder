#!/usr/bin/make -f

NAME = AmpTrem

FILES_DSP = \
	AmpTremPlugin.cpp

FILES_UI = \
	AmpTrem_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
