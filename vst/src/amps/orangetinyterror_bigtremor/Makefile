#!/usr/bin/make -f
# Citrus Big Tremor — Orange Tiny Terror single-channel guitar head model (DPF)

NAME = CitrusBigTremor

FILES_DSP = \
	BigTremorPlugin.cpp

FILES_UI = \
	BigTremor_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
