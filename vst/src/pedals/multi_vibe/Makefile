#!/usr/bin/make -f

NAME = MultiVibe

FILES_DSP = \
	MultiVibePlugin.cpp

FILES_UI = \
	MultiVibe_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
