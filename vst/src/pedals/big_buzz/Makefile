#!/usr/bin/make -f

NAME = BigBuzz

FILES_DSP = \
	BigBuzzPlugin.cpp

FILES_UI = \
	BigBuzz_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
