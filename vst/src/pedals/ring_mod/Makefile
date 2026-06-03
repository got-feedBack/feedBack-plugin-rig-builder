#!/usr/bin/make -f

NAME = RingMod

FILES_DSP = \
	RingModPlugin.cpp

FILES_UI = \
	RingMod_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
