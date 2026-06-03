#!/usr/bin/make -f

NAME = AutoVibe

FILES_DSP = \
	AutoVibePlugin.cpp

FILES_UI = \
	AutoVibe_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
