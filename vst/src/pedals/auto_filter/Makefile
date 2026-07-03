#!/usr/bin/make -f

NAME = AutoFilter

FILES_DSP = \
	AutoFilterPlugin.cpp

FILES_UI = \
	AutoFilter_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
