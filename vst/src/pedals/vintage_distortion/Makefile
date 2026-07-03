#!/usr/bin/make -f

NAME = VintageDistortion

FILES_DSP = \
	VintageDistortionPlugin.cpp

FILES_UI = \
	VintageDistortion_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
