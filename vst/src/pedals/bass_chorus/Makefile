#!/usr/bin/make -f

NAME = BassChorus

FILES_DSP = \
	BassChorusPlugin.cpp

FILES_UI = \
	BassChorus_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
