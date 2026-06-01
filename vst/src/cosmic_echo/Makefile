#!/usr/bin/make -f

NAME = CosmicEcho

FILES_DSP = \
	CosmicEchoPlugin.cpp

FILES_UI = \
	CosmicEcho_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
