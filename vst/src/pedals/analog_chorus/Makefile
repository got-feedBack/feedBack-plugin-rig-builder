#!/usr/bin/make -f

NAME = AnalogChorus

FILES_DSP = \
	AnalogChorusPlugin.cpp

FILES_UI = \
	AnalogChorus_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
