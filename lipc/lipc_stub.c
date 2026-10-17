/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * liblipc stub for Biscuit's Fire OS 6 libasp.
 *
 * libasp's IpcHandlerLipc opens Amazon's LIPC (Lab126 IPC, carried over
 * D-Bus) in the audio HAL constructor and asserts when that fails. Fire OS
 * runs a system D-Bus for it; LineageOS does not, and nothing here listens
 * for libasp's events or properties (beam direction, ASP state). The Fire
 * OS 7 libasp on the Echo Show trees has no LIPC at all.
 *
 * Every call libasp makes succeeds and goes nowhere: no events are ever
 * delivered, so the getters that only run inside event callbacks report
 * that the parameter does not exist. Signatures follow Lab126's
 * openlipc.h; LipcOpenEx stores its status through its second argument,
 * as the Fire OS library does.
 */

#include <stddef.h>

typedef int LIPCcode;
typedef struct LIPC LIPC;
typedef struct LIPCevent LIPCevent;

#define LIPC_OK 0
#define LIPC_ERROR_UNKNOWN 1

struct LIPC {
    int unused;
};

static LIPC lipc_handle;

LIPC* LipcOpenEx(const char* service, LIPCcode* code) {
    (void)service;
    if (code) *code = LIPC_OK;
    return &lipc_handle;
}

void LipcClose(LIPC* lipc) {
    (void)lipc;
}

LIPCcode LipcRegisterIntProperties(LIPC* lipc, const void* properties, int count) {
    (void)lipc; (void)properties; (void)count;
    return LIPC_OK;
}

LIPCcode LipcRegisterStringProperties(LIPC* lipc, const void* properties, int count) {
    (void)lipc; (void)properties; (void)count;
    return LIPC_OK;
}

LIPCcode LipcUnregisterProperty(LIPC* lipc, const char* property, void** data) {
    (void)lipc; (void)property;
    if (data) *data = NULL;
    return LIPC_OK;
}

LIPCcode LipcSetStringProperty(LIPC* lipc, const char* service, const char* property,
                               const char* value) {
    (void)lipc; (void)service; (void)property; (void)value;
    return LIPC_OK;
}

LIPCcode LipcSubscribeExt(LIPC* lipc, const char* service, const char* name,
                          void* callback, void* data) {
    (void)lipc; (void)service; (void)name; (void)callback; (void)data;
    return LIPC_OK;
}

LIPCcode LipcUnsubscribeExt(LIPC* lipc, const char* service, const char* name,
                            void** data) {
    (void)lipc; (void)service; (void)name;
    if (data) *data = NULL;
    return LIPC_OK;
}

LIPCcode LipcCreateAndSendEvent(LIPC* lipc, const char* name) {
    (void)lipc; (void)name;
    return LIPC_OK;
}

LIPCcode LipcCreateAndSendEventWithParameters(LIPC* lipc, const char* name,
                                              const char* format, ...) {
    (void)lipc; (void)name; (void)format;
    return LIPC_OK;
}

LIPCcode LipcGetIntParam(LIPCevent* event, int* value) {
    (void)event;
    if (value) *value = 0;
    return LIPC_ERROR_UNKNOWN;
}

LIPCcode LipcGetStringParam(LIPCevent* event, char** value) {
    (void)event;
    if (value) *value = NULL;
    return LIPC_ERROR_UNKNOWN;
}
