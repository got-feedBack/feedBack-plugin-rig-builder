#!/usr/bin/make -f

NAME = NoiseGate

FILES_DSP = \
	NoiseGatePlugin.cpp

FILES_UI = \
	NoiseGate_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
