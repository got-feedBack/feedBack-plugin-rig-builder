#!/usr/bin/make -f
# Marsten DBS 7400 — Marshall DBS 7400 bass head model (DPF plugin)

NAME = MarstenDBS7400

FILES_DSP = \
	DbsPlugin.cpp

FILES_UI = \
	Dbs_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
