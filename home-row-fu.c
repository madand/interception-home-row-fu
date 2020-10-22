// home-row-fu.c

// Author: Andriy B. Kmit' <dev@madand.net>
// Public Domain / CC0 https://creativecommons.org/publicdomain/zero/1.0/

#include <stdio.h>  // setbuf, fwrite, fread
#include <stdlib.h>  // EXIT_SUCCESS, EXIT_FAILURE
#include <stdbool.h> // bool, true, false
#include <stdint.h>
#include <linux/input.h>  // struct input_event
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
/// Constants

// Delay (in milliseconds) before key can become a modifier.
#define BECOME_MODIFIER_DELAY_MSEC 150

// Maximum time (in milliseconds) when key release still inserts a letter.
#define REMAIN_REAL_KEY_MSEC 700

// Key event state constants
#define KEY_EVENT_DOWN 1
#define KEY_EVENT_UP 0
#define KEY_EVENT_REPEAT 3

// Microseconds per second
#define USEC_PER_SEC 1e6

////////////////////////////////////////////////////////////////////////////////
/// Key event prototype variables declarations

/* SYN event should be sent after each emulated event. */
static const struct input_event ev_syn = {
    .type = EV_SYN, .code = SYN_REPORT, .value = 0};

////////////////////////////////////////////////////////////////////////////////
/// Global state

/* Most recent MSC_SCAN event. We receive scan evens before every key event, so
 * we use it for timing comparisons. */
static struct input_event recent_scan;

////////////////////////////////////////////////////////////////////////////////
/// Helper functions

/* Return the time difference in microseconds. */
suseconds_t time_diff(const struct timeval *earlier,
                      const struct timeval *later) {
    return later->tv_usec - earlier->tv_usec +
           (later->tv_sec - earlier->tv_sec) * USEC_PER_SEC;
}

/* Read next event from STDIN. Return 1 on success. */
int read_event(struct input_event *event) {
    return fread(event, sizeof(struct input_event), 1, stdin) == 1;
}

/* Write event to STDOUT. If write failed, exit the program. */
void write_event(const struct input_event *event) {
    if (fwrite(event, sizeof(struct input_event), 1, stdout) != 1)
        exit(EXIT_FAILURE);
}

/* Output events queue. */
static struct input_event ev_queue[20];
unsigned int ev_queue_size = 0;

/* Add event to the output events queue. */
void enqueue_event(const struct input_event *event) {
    ev_queue[ev_queue_size++] = *event;
}

/* Add event to the output events queue and SYN event afterwards. */
void enqueue_event_and_syn(const struct input_event *event) {
    ev_queue[ev_queue_size++] = *event;
    ev_queue[ev_queue_size++] = ev_syn;
}

void flush_event_queue() {
    if (fwrite(ev_queue, sizeof(struct input_event), ev_queue_size, stdout) !=
        ev_queue_size)
        exit(EXIT_FAILURE);
    ev_queue_size = 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Predicates

/* Return true if event is Key Down event. */
bool is_key_down_event(const struct input_event *event) {
    return event->value == KEY_EVENT_DOWN;
}

/* Return true if event is Key Up event. */
bool is_key_up_event(const struct input_event *event) {
    return event->value == KEY_EVENT_UP;
}

/* Return true if event is Key Repeat event. */
bool is_key_repeat_event(const struct input_event *event) {
    return event->value == KEY_EVENT_REPEAT;
}

/* Return true if event is for the given key.
 * Key codes can be found in <input-event-codes.h>. */
bool is_event_for_key(const struct input_event *event, const uint key_code) {
    return event->code == key_code;
}

/* Delay-based guard to protect the key from becoming a modifier too early.
 * This delay is crucial if you type fast enough. */
bool can_become_modifier(const struct timeval *recent_down_time) {
    return time_diff(recent_down_time, &recent_scan.time) >
           (BECOME_MODIFIER_DELAY_MSEC * 1e3);
}

/* Delay-based guard to protect the key from inserting a letter if pressed for a
 * longish time. */
bool can_send_real_down(const struct timeval *recent_down_time) {
    return time_diff(recent_down_time, &recent_scan.time) <
           (REMAIN_REAL_KEY_MSEC * 1e3);
}

struct key_state {
    /* Key code of the physical key. */
    const uint16_t key;
    /* Time of the most recent Key Down event. */
    struct timeval recent_down_time;
    /* Flag indicating that the key is currently down. */
    bool is_down;
    /* Flag indicating that we sent a real event (a letter). If this is set
     * the key cannot become a modifier until released. */
    bool has_sent_real_down;
    /* Flag indicating that the key has became a modifier until it's
     * released. */
    bool has_became_modifier;
    // Down and Up events conveniently prepared for sending when the time comes:
    const struct input_event ev_key_down;
    const struct input_event ev_key_up;
    const struct input_event ev_modifier_down;
    const struct input_event ev_modifier_up;
} key_config[] = {
#define DEFINE_MAPPING(key_code, modifier_code)        \
    {                                                  \
        .key              = key_code,                  \
        .ev_key_down      = {.type  = EV_KEY,          \
                        .code  = key_code,        \
                        .value = KEY_EVENT_DOWN}, \
        .ev_key_up        = {.type  = EV_KEY,          \
                      .code  = key_code,        \
                      .value = KEY_EVENT_UP},   \
        .ev_modifier_down = {.type  = EV_KEY,          \
                             .code  = modifier_code,   \
                             .value = KEY_EVENT_DOWN}, \
        .ev_modifier_up   = {.type  = EV_KEY,          \
                           .code  = modifier_code,   \
                           .value = KEY_EVENT_UP},   \
    },

#include "key_config.h"

#undef DEFINE_MAPPING
};

static const size_t key_config_size =
    sizeof(key_config) / sizeof(key_config[0]);

////////////////////////////////////////////////////////////////////////////////
/// Key handlers

void handle_key_down(const struct input_event *event, struct key_state *state) {
    if (!is_event_for_key(event, state->key) && state->is_down) {
        if (state->has_became_modifier || state->has_sent_real_down)
            return;

        if (can_become_modifier(&state->recent_down_time)) {
            enqueue_event_and_syn(&state->ev_modifier_down);
            state->has_became_modifier = true;
            return;
        }

        if (can_send_real_down(&state->recent_down_time)) {
            enqueue_event_and_syn(&state->ev_key_down);
            state->has_sent_real_down = true;
            return;
        }
    }

    if (is_event_for_key(event, state->key)) {
        state->recent_down_time = event->time;
        state->is_down          = true;
    }
}

void handle_key_up(const struct input_event *event, struct key_state *state) {
    if (!is_event_for_key(event, state->key)) {
        return;
    }

    state->is_down = false;

    if (state->has_became_modifier) {
        enqueue_event_and_syn(&state->ev_modifier_up);
        state->has_became_modifier = false;
        return;
    }

    if (state->has_sent_real_down) {
        enqueue_event_and_syn(&state->ev_key_up);
        state->has_sent_real_down = false;
        return;
    }

    if (can_send_real_down(&state->recent_down_time)) {
        enqueue_event_and_syn(&state->ev_key_down);
        enqueue_event_and_syn(&state->ev_key_up);
    }
}

bool handle_key(const struct input_event *event, struct key_state *state) {
    if (is_key_down_event(event)) {
        handle_key_down(event, state);
    } else if (is_key_up_event(event)) {
        handle_key_up(event, state);
    }

    return is_event_for_key(event, state->key);
}

////////////////////////////////////////////////////////////////////////////////
/// Entry point

int main(void) {
    struct input_event curr_event;
    bool found_handler;

    setbuf(stdin, NULL), setbuf(stdout, NULL), setbuf(stderr, NULL);

    while (read_event(&curr_event)) {
        if (curr_event.type == EV_MSC && curr_event.code == MSC_SCAN) {
            recent_scan = curr_event;
            continue;
        }

        if (curr_event.type != EV_KEY) {
            write_event(&curr_event);
            continue;
        }

        found_handler = false;
        for (size_t i = 0; i < key_config_size; i++) {
            found_handler =
                handle_key(&curr_event, &key_config[i]) || found_handler;
        }

        if (!found_handler) {
            enqueue_event(&recent_scan);
            enqueue_event(&curr_event);
        }

        flush_event_queue();
    }

    return EXIT_SUCCESS;
}

// Local Variables:
// compile-command: "gcc -Wall -o home-row-fu home-row-fu.c"
// End:
