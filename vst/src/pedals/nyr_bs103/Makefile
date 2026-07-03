#!/usr/bin/make -f

NAME = NYRBS103

FILES_DSP = \
	BassSynthPlugin.cpp

FILES_UI = \
	BassSynth_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
