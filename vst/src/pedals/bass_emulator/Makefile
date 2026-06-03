#!/usr/bin/make -f

NAME = BassEmulator

FILES_DSP = \
	BassEmulatorPlugin.cpp

FILES_UI = \
	BassEmulator_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
