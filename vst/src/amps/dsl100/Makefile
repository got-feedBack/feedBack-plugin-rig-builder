#!/usr/bin/make -f

NAME = DSL100

FILES_DSP = \
	DSL100Plugin.cpp

FILES_UI = \
	DSL100_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
