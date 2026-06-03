#!/usr/bin/make -f

NAME = BassPhase

FILES_DSP = \
	BassPhasePlugin.cpp

FILES_UI = \
	BassPhase_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
