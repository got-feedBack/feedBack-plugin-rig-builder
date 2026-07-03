#!/usr/bin/make -f

NAME = JC90

FILES_DSP = \
	JC90Plugin.cpp

FILES_UI = \
	JC90_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
