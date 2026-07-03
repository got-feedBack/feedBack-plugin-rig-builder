#!/usr/bin/make -f
# Tracer V8 — Trace Elliot V-Type V8 all-valve bass head model (DPF plugin)

NAME = TracerV8

FILES_DSP = \
	TracerPlugin.cpp

FILES_UI = \
	Tracer_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
