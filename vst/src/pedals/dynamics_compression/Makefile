#!/usr/bin/make -f

NAME = DynamicsCompression

FILES_DSP = \
	DynamicsCompressionPlugin.cpp

FILES_UI = \
	DynamicsCompression_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
