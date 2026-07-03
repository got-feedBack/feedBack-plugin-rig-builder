#!/usr/bin/make -f

NAME = AmpVibe

FILES_DSP = \
	AmpVibePlugin.cpp

FILES_UI = \
	AmpVibe_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
