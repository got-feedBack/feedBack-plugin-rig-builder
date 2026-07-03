#!/usr/bin/make -f

NAME = SpringReverb

FILES_DSP = \
	SpringReverbPlugin.cpp

FILES_UI = \
	SpringReverb_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
