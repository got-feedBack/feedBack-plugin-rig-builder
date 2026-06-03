#!/usr/bin/make -f

NAME = BassFuzz

FILES_DSP = \
	BassFuzzPlugin.cpp

FILES_UI = \
	BassFuzz_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
