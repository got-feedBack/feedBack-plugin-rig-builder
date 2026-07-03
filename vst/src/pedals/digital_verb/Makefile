#!/usr/bin/make -f

NAME = DigitalVerb

FILES_DSP = \
	DigitalVerbPlugin.cpp

FILES_UI = \
	DigitalVerb_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
