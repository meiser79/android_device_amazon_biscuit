/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#define BTM_DEF_LOCAL_NAME "Echo Dot"

// A2DP sink (speaker) with AVRCP controller
#define BTA_AV_SINK_INCLUDED TRUE
#define BTA_AR_INCLUDED TRUE

// Class of device: audio/video, loudspeaker, rendering + audio services
#define BTA_DM_COD {0x20, 0x04, 0x14}

// Headless speaker: pair without a numeric comparison prompt.
#define BTM_LOCAL_IO_CAPS BTM_IO_CAP_NONE
