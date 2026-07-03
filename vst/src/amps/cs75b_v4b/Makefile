#!/usr/bin/make -f
# Sampleg V-4B — Ampeg V-4B all-tube bass head model (DPF plugin)

NAME = SamplegV4B

FILES_DSP = \
	V4bPlugin.cpp

FILES_UI = \
	V4b_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
