#!/usr/bin/make -f

NAME = BassDistortion

FILES_DSP = \
	BassDistortionPlugin.cpp

FILES_UI = \
	BassDistortion_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
