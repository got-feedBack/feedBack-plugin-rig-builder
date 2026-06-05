#!/usr/bin/make -f
# PeeBee T-Minus — Peavey T-Max two-channel bass head model (DPF plugin)

NAME = PeeBeeTMinus

FILES_DSP = \
	TMaxPlugin.cpp

FILES_UI = \
	TMax_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
