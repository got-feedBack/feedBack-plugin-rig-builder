#!/usr/bin/make -f

NAME = CustomDrive

FILES_DSP = \
	CustomDrivePlugin.cpp

FILES_UI = \
	CustomDrive_ui.cpp

DPF_PATH ?= ../DPF

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += vst3

all: $(TARGETS)
