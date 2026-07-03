#!/usr/bin/make -f
# Hipzon GA-8 — Gibson GA-8 Discoverer guitar amp (tremolo) model (DPF)

NAME = HipzonGA8

FILES_DSP = \
	Ga8Plugin.cpp

FILES_UI = \
	Ga8_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
