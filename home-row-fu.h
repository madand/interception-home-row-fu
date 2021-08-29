/*
  MIT License

  Copyright (c) 2020 - 2021 Andriy B. Kmit'

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

// Author: Andriy B. Kmit' <dev@madand.net>
// URL: https://github.com/madand/interception-home-row-fu

#include <stdbool.h>
#include <stdint.h>
#include <linux/input.h>  // struct input_event, KEY_A ...

////////////////////////////////////////////////////////////////////////////////
/// User configurable constants

#define DEFAULT_CONFIG_FILE "/etc/home-row-fu.toml"

#define DEFAULT_BURST_TYPING_MSEC 200
#define DEFAULT_CAN_INSERT_LETTER_MSEC 700
#define DEFAULT_IMMEDIATELY_SEND_MODIFIER false

////////////////////////////////////////////////////////////////////////////////
// Internal constants

/* Key event value constants */
#define EVENT_VALUE_KEY_UP 0
#define EVENT_VALUE_KEY_DOWN 1
#define EVENT_VALUE_KEY_REPEAT 3

/* Microseconds per millisecond */
#define US_PER_MS 1000
/* Microseconds per second */
#define US_PER_SECOND (1000 * US_PER_MS)

#define EVENT_BUFFER_SIZE 16
#define TOML_ERROR_BUFFER_SIZE 200

#define ensure_buffer_not_full(buf_var, size_var)                        \
    if (size_var >= EVENT_BUFFER_SIZE) {                                 \
        fprintf(stderr, "Error in %s(): buffer " #buf_var " is full.\n", \
                __func__);                                               \
        exit(EXIT_FAILURE);                                              \
    }

typedef struct input_event input_event;

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
     * but should probably be false for Alt since GUI apps respond to Alt press
     * by activating the main menu. */
    bool immediately_send_modifier;
    // Prototypes of Down and Up events.
    input_event ev_real_down;
    input_event ev_real_up;
    input_event ev_modifier_down;
    input_event ev_modifier_up;
};

typedef struct key_state key_state;
