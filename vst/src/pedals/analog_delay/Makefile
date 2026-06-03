#!/usr/bin/make -f

NAME = AnalogDelay

FILES_DSP = \
	AnalogDelayPlugin.cpp

FILES_UI = \
	AnalogDelay_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
