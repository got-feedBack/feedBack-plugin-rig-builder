#!/usr/bin/make -f

NAME = VintageFlanger

FILES_DSP = \
	VintageFlangerPlugin.cpp

FILES_UI = \
	VintageFlanger_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
