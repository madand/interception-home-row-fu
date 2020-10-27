// home-row-fu.h

// Author: Andriy B. Kmit' <dev@madand.net>
// URL: https://github.com/madand/interception-home-row-fu
// Public Domain / CC0 https://creativecommons.org/publicdomain/zero/1.0/

#pragma once

#include <stdint.h>       // uint16_t
#include <linux/input.h>  // struct input_event

////////////////////////////////////////////////////////////////////////////////
/// User configurable constants

#define DEFAULT_CONFIG_FILE "/etc/home-row-fu.toml"

#define DEFAULT_BURST_TYPING_MSEC 150

#define DEFAULT_CAN_INSERT_LETTER_MSEC 700

////////////////////////////////////////////////////////////////////////////////
// Internal constants

/* Key event value constants */
#define EVENT_VALUE_KEY_UP 0
#define EVENT_VALUE_KEY_DOWN 1
#define EVENT_VALUE_KEY_REPEAT 3

/* Microseconds per millisecond */
#define MSEC_PER_USEC 1000
/* Microseconds per second */
#define USEC_PER_SEC (1000 * MSEC_PER_USEC)

/* Size of the output events buffers (each element is of type struct
 * input_event).
 * With current implementation size of 12 should be precisely what is
 * needed, but a few spare bytes won't hurt anyone. */
#define EVENT_BUFFER_SIZE 16

#define TOML_ERROR_BUFFER_SIZE 200

#define check_buffer_not_full(buf_var, size_var)                         \
    if (size_var >= EVENT_BUFFER_SIZE) {                                 \
        fprintf(stderr, "Error in %s(): buffer " #buf_var " is full.\n", \
                __func__);                                               \
        exit(EXIT_FAILURE);                                              \
    }

////////////////////////////////////////////////////////////////////////////////
/// Event handling configuration

struct key_state {
    /* Key code of the physical key. */
    uint16_t key;
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
     * activating the main menu. */
    bool simulate_modifier_press_on_key_down;
    // Down and Up events conveniently prepared for sending when the time comes:
    struct input_event ev_real_down;
    struct input_event ev_real_up;
    struct input_event ev_modifier_down;
    struct input_event ev_modifier_up;
};
