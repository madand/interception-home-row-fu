// home-row-fu.h

// Author: Andriy B. Kmit' <dev@madand.net>
// URL: https://github.com/madand/interception-home-row-fu
// Public Domain / CC0 https://creativecommons.org/publicdomain/zero/1.0/

#pragma once

#include <linux/input.h>  // struct input_event

////////////////////////////////////////////////////////////////////////////////
/// User configurable constants

#define DEFAULT_CONFIG_FILE "/etc/home-row-fu.toml"

#define DEFAULT_BURST_TYPING_MSEC 200

#define DEFAULT_CAN_INSERT_LETTER_MSEC 700

#define IMMEDIATELY_SEND_MODIFIER_DEFAULT false

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

// Aliases for struct types
typedef struct input_event input_event;
typedef struct key_state key_state;
