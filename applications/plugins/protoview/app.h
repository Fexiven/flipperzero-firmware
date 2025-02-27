/* Copyright (C) 2022-2023 Salvatore Sanfilippo -- All Rights Reserved
 * See the LICENSE file for information about the license. */

#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <input/input.h>
#include <gui/gui.h>
#include <stdlib.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>
#include <lib/subghz/subghz_setting.h>
#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>
#include <lib/subghz/registry.h>
#include "app_buffer.h"

#define TAG "ProtoView"
#define PROTOVIEW_RAW_VIEW_DEFAULT_SCALE 100
#define BITMAP_SEEK_NOT_FOUND UINT32_MAX

#define DEBUG_MSG 1

typedef struct ProtoViewApp ProtoViewApp;

/* Subghz system state */
typedef enum {
    TxRxStateIDLE,
    TxRxStateRx,
    TxRxStateSleep,
} TxRxState;

/* Currently active view. */
typedef enum {
    ViewRawPulses,
    ViewInfo,
    ViewFrequencySettings,
    ViewModulationSettings,
    ViewDirectSampling,
    ViewLast, /* Just a sentinel to wrap around. */
} ProtoViewCurrentView;

/* Used by app_switch_view() */
typedef enum { AppNextView, AppPrevView } SwitchViewDirection;

typedef struct {
    const char* name;
    FuriHalSubGhzPreset preset;
    uint8_t* custom;
} ProtoViewModulation;

extern ProtoViewModulation ProtoViewModulations[]; /* In app_subghz.c */

/* This is the context of our subghz worker and associated thread.
 * It receives data and we get our protocol "feed" callback called
 * with the level (1 or 0) and duration. */
struct ProtoViewTxRx {
    bool freq_mod_changed; /* The user changed frequency and/or modulation
                                   from the interface. There is to restart the
                                   radio with the right parameters. */
    SubGhzWorker* worker; /* Our background worker. */
    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    TxRxState txrx_state; /* Receiving, idle or sleeping? */

    /* Timer sampling mode state. */
    bool debug_timer_sampling; /* Read data from GDO0 in a busy loop. Only
                                   for testing. */
    uint32_t last_g0_change_time; /* Last high->low (or reverse) switch. */
    bool last_g0_value; /* Current value (high or low): we are
                                     checking the duration in the timer
                                     handler. */
};

typedef struct ProtoViewTxRx ProtoViewTxRx;

/* This stucture is filled by the decoder for specific protocols with the
 * informations about the message. ProtoView will display such information
 * in the message info view. */
#define PROTOVIEW_MSG_STR_LEN 32
typedef struct ProtoViewMsgInfo {
    char name[PROTOVIEW_MSG_STR_LEN]; /* Protocol name and version. */
    char raw[PROTOVIEW_MSG_STR_LEN]; /* Protocol specific raw representation.*/
    /* The following is what the decoder wants to show to user. Each decoder
     * can use the number of fileds it needs. */
    char info1[PROTOVIEW_MSG_STR_LEN]; /* Protocol specific info line 1. */
    char info2[PROTOVIEW_MSG_STR_LEN]; /* Protocol specific info line 2. */
    char info3[PROTOVIEW_MSG_STR_LEN]; /* Protocol specific info line 3. */
    char info4[PROTOVIEW_MSG_STR_LEN]; /* Protocol specific info line 4. */
    uint64_t len; /* Bits consumed from the stream. */
} ProtoViewMsgInfo;

struct ProtoViewApp {
    /* GUI */
    Gui* gui;
    ViewPort* view_port; /* We just use a raw viewport and we render
                                everything into the low level canvas. */
    ProtoViewCurrentView current_view; /* Active view ID. */
    FuriMessageQueue* event_queue; /* Keypress events go here. */

    /* Radio related. */
    ProtoViewTxRx* txrx; /* Radio state. */
    SubGhzSetting* setting; /* A list of valid frequencies. */

    /* Generic app state. */
    int running; /* Once false exists the app. */
    uint32_t signal_bestlen; /* Longest coherent signal observed so far. */
    uint32_t signal_last_scan_idx; /* Index of the buffer last time we
                                      performed the scan. */
    bool signal_decoded; /* Was the current signal decoded? */
    ProtoViewMsgInfo signal_info; /* Decoded message, if signal_decoded true. */
    bool direct_sampling_enabled; /* This special view needs an explicit
                                     acknowledge to work. */

    /* Raw view apps state. */
    uint32_t us_scale; /* microseconds per pixel. */
    uint32_t signal_offset; /* Long press left/right panning in raw view. */

    /* Configuration view app state. */
    uint32_t frequency; /* Current frequency. */
    uint8_t modulation; /* Current modulation ID, array index in the
                                ProtoViewModulations table. */
};

typedef struct ProtoViewDecoder {
    const char* name; /* Protocol name. */
    /* The decode function takes a buffer that is actually a bitmap, with
     * high and low levels represented as 0 and 1. The number of high/low
     * pulses represented by the bitmap is passed as the 'numbits' argument,
     * while 'numbytes' represents the total size of the bitmap pointed by
     * 'bits'. So 'numbytes' is mainly useful to pass as argument to other
     * functions that perform bit extraction with bound checking, such as
     * bitmap_get() and so forth. */
    bool (*decode)(uint8_t* bits, uint32_t numbytes, uint32_t numbits, ProtoViewMsgInfo* info);
} ProtoViewDecoder;

extern RawSamplesBuffer *RawSamples, *DetectedSamples;

/* app_radio.c */
void radio_begin(ProtoViewApp* app);
uint32_t radio_rx(ProtoViewApp* app);
void radio_idle(ProtoViewApp* app);
void radio_rx_end(ProtoViewApp* app);
void radio_sleep(ProtoViewApp* app);
void raw_sampling_worker_start(ProtoViewApp* app);
void raw_sampling_worker_stop(ProtoViewApp* app);

/* signal.c */
uint32_t duration_delta(uint32_t a, uint32_t b);
void reset_current_signal(ProtoViewApp* app);
void scan_for_signal(ProtoViewApp* app);
bool bitmap_get(uint8_t* b, uint32_t blen, uint32_t bitpos);
void bitmap_set(uint8_t* b, uint32_t blen, uint32_t bitpos, bool val);
void bitmap_set_pattern(uint8_t* b, uint32_t blen, const char* pat);
void bitmap_reverse_bytes(uint8_t* p, uint32_t len);
bool bitmap_match_bits(uint8_t* b, uint32_t blen, uint32_t bitpos, const char* bits);
uint32_t bitmap_seek_bits(
    uint8_t* b,
    uint32_t blen,
    uint32_t startpos,
    uint32_t maxbits,
    const char* bits);
uint32_t convert_from_line_code(
    uint8_t* buf,
    uint64_t buflen,
    uint8_t* bits,
    uint32_t len,
    uint32_t offset,
    const char* zero_pattern,
    const char* one_pattern);
uint32_t convert_from_diff_manchester(
    uint8_t* buf,
    uint64_t buflen,
    uint8_t* bits,
    uint32_t len,
    uint32_t off,
    bool previous);

/* view_*.c */
void render_view_raw_pulses(Canvas* const canvas, ProtoViewApp* app);
void process_input_raw_pulses(ProtoViewApp* app, InputEvent input);
void render_view_settings(Canvas* const canvas, ProtoViewApp* app);
void process_input_settings(ProtoViewApp* app, InputEvent input);
void render_view_info(Canvas* const canvas, ProtoViewApp* app);
void process_input_info(ProtoViewApp* app, InputEvent input);
void render_view_direct_sampling(Canvas* const canvas, ProtoViewApp* app);
void process_input_direct_sampling(ProtoViewApp* app, InputEvent input);
void view_enter_direct_sampling(ProtoViewApp* app);
void view_exit_direct_sampling(ProtoViewApp* app);
void view_exit_settings(ProtoViewApp* app);

/* ui.c */
void canvas_draw_str_with_border(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    const char* str,
    Color text_color,
    Color border_color);

/* crc.c */
uint8_t crc8(const uint8_t* data, size_t len, uint8_t init, uint8_t poly);
