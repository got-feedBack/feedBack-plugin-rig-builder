#!/usr/bin/make -f

NAME = SuperBuzz

FILES_DSP = \
	SuperBuzzPlugin.cpp

FILES_UI = \
	SuperBuzz_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
