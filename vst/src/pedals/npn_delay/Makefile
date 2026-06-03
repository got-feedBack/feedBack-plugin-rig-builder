#!/usr/bin/make -f

NAME = NpnDelay

FILES_DSP = \
	NpnDelayPlugin.cpp

FILES_UI = \
	NpnDelay_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
