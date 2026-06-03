#!/usr/bin/make -f

NAME = BassMultiComp

FILES_DSP = \
	BassMultiCompPlugin.cpp

FILES_UI = \
	BassMultiComp_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
