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

#pragma once

#include <linux/input.h>  // struct input_event

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

/* Size of the output events buffers (each element is of type struct
 * input_event).
 * With current implementation size of 12 should be precisely what is
 * needed, but a few spare bytes won't hurt anyone. */
#define EVENT_BUFFER_SIZE 16

#define TOML_ERROR_BUFFER_SIZE 200

#define ensure_buffer_not_full(buf_var, size_var)                         \
    if (size_var >= EVENT_BUFFER_SIZE) {                                 \
        fprintf(stderr, "Error in %s(): buffer " #buf_var " is full.\n", \
                __func__);                                               \
        exit(EXIT_FAILURE);                                              \
    }

// Aliases for struct types
typedef struct input_event input_event;
typedef struct key_state key_state;
