#!/usr/bin/make -f

NAME = ModernFlanger

FILES_DSP = \
	ModernFlangerPlugin.cpp

FILES_UI = \
	ModernFlanger_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
