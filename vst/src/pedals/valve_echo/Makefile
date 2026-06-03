#!/usr/bin/make -f

NAME = ValveEcho

FILES_DSP = \
	ValveEchoPlugin.cpp

FILES_UI = \
	ValveEcho_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
