#!/usr/bin/make -f

NAME = ModernWah

FILES_DSP = \
	ModernWahPlugin.cpp

FILES_UI = \
	ModernWah_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
