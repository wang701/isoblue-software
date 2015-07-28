LOCAL_PATH := $(call my-dir)

PRIVATE_LOCAL_CFLAGS := -O2 -g -W -Wall		\
			-DSO_RXQ_OVFL=40	\
			-DPF_CAN=29		\
			-DAF_CAN=PF_CAN

TARGET_PLATFORM := android-19

#
# sc_mod_test
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := sc_mod_test.c
LOCAL_MODULE := sc_mod_test
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include/
LOCAL_CFLAGS := $(PRIVATE_LOCAL_CFLAGS)

include $(BUILD_EXECUTABLE)