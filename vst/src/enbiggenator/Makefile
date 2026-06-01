#!/usr/bin/make -f

NAME = Enbiggenator

FILES_DSP = \
	EnbiggenatorPlugin.cpp

FILES_UI = \
	Enbiggenator_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
