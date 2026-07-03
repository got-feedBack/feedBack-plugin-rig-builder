#!/usr/bin/make -f

NAME = BuzzTone

FILES_DSP = \
	BuzzTonePlugin.cpp

FILES_UI = \
	BuzzTone_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
