// home-row-fu.h

// Author: Andriy B. Kmit' <dev@madand.net>
// URL: https://github.com/madand/interception-home-row-fu
// Public Domain / CC0 https://creativecommons.org/publicdomain/zero/1.0/

#pragma once

#include <stdint.h>       // uint16_t
#include <linux/input.h>  // struct input_event

////////////////////////////////////////////////////////////////////////////////
/// User configurable constants

/* Delay (in milliseconds) before a key can become a modifier.
 * This allows for burst typing, but slows you down when you actually want a key
 * to act as modifier. */
#define BECOME_MODIFIER_DELAY_MSEC 150

/* Maximum time (in milliseconds) when a key release still inserts a letter.
 * For example, when you press F (acts as Left Ctrl by default), hold it for
 * more than 0.7 seconds then change your mind and just release it - nothing
 * will be inserted. */
#define REMAIN_REAL_KEY_MSEC 700

////////////////////////////////////////////////////////////////////////////////
// Internal constants

/* Key event state constants */
#define KEY_EVENT_DOWN 1
#define KEY_EVENT_UP 0
#define KEY_EVENT_REPEAT 3

/* Microseconds per second */
#define USEC_PER_SEC 1e6
/* Microseconds per millisecond */
#define MSEC_PER_USEC 1e3

/* Size of the output events buffers (each element is of type struct
 * input_event).
 * With current implementation size of 12 should be precisely what is
 * needed, but a few spare bytes won't hurt anyone. */
#define EVENT_BUFFER_SIZE 16

#define check_buffer_not_full(buf_var, size_var)                      \
    if (size_var >= EVENT_BUFFER_SIZE) {                              \
        fprintf(stderr, "Error in %s(): buffer " #buf_var " is full", \
                __func__);                                            \
        exit(EXIT_FAILURE);                                           \
    }

////////////////////////////////////////////////////////////////////////////////
/// Event handling configuration

struct key_state {
    /* Key code of the physical key. */
    const uint16_t key;
    /* Time of the most recent Key Down event. */
    struct timeval recent_down_time;
    /* Flag indicating that the key is currently down. */
    bool is_held;
    /* Flag indicating that we sent modifier Down event. If this is set we must
     * eventually send a modifier Up event. */
    bool is_modifier_held;
    /* Flag indicating that we sent a real Down event (a letter). If this is set
     * the key cannot become a modifier until released. */
    bool has_sent_real_down;
    /* Flag indicating that the key has became a modifier until released. */
    bool is_locked_to_modifier;
    /* Flag indicating that we want to simulate modifier press immediately after
     * the key was pressed. Good with Ctrl to allow a Ctrl+Mouse scroll etc.,
     * but should probably be false for Alt since GUI apps react to Alt by
     * activating the main menu shortcuts. */
    bool should_hold_modifier_on_key_down;
    // Down and Up events conveniently prepared for sending when the time comes:
    const struct input_event ev_real_down;
    const struct input_event ev_real_up;
    const struct input_event ev_modifier_down;
    const struct input_event ev_modifier_up;
};

#define DEFINE_MAPPING(key_code, modifier_code, hold_modifier_on_key_down) \
    {                                                                      \
        .key                              = key_code,                      \
        .should_hold_modifier_on_key_down = hold_modifier_on_key_down,     \
        .ev_real_down                     = {.type  = EV_KEY,              \
                         .code  = key_code,            \
                         .value = KEY_EVENT_DOWN},     \
        .ev_real_up                       = {.type  = EV_KEY,              \
                       .code  = key_code,            \
                       .value = KEY_EVENT_UP},       \
        .ev_modifier_down                 = {.type  = EV_KEY,              \
                             .code  = modifier_code,       \
                             .value = KEY_EVENT_DOWN},     \
        .ev_modifier_up                   = {.type  = EV_KEY,              \
                           .code  = modifier_code,       \
                           .value = KEY_EVENT_UP},       \
    },

struct key_state key_config[] = {
// Edit key_config.h to change the mappings to your taste.
#include "key_config.h"
};

#define KEY_CONFIG_SIZE (sizeof(key_config) / sizeof(key_config[0]))
