#!/usr/bin/make -f

NAME = AcousticSimulator

FILES_DSP = \
	AcousticSimulatorPlugin.cpp

FILES_UI = \
	AcousticSimulator_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
