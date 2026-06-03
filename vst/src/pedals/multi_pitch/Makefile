#!/usr/bin/make -f

NAME = MultiPitch

FILES_DSP = \
	MultiPitchPlugin.cpp

FILES_UI = \
	MultiPitch_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
