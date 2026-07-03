#!/usr/bin/make -f

NAME = EdenWTDI

FILES_DSP = \
	EdenWtdiPlugin.cpp

FILES_UI = \
	EdenWtdi_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
