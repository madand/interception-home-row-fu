// home-row-fu.c

// Author: Andriy B. Kmit' <dev@madand.net>
// URL: https://github.com/madand/interception-home-row-fu
// Public Domain / CC0 https://creativecommons.org/publicdomain/zero/1.0/

#include <stdio.h>        // setbuf, fwrite, fread
#include <stdlib.h>       // EXIT_SUCCESS, EXIT_FAILURE
#include <stdbool.h>      // bool, true, false
#include <stdint.h>       // size_t
#include <assert.h>       // assert
#include <linux/input.h>  // struct input_event

#include "home-row-fu.h"

////////////////////////////////////////////////////////////////////////////////
/// Global state

/* SYN event should be sent after each emulated event. */
static const struct input_event ev_syn = {
    .type = EV_SYN, .code = SYN_REPORT, .value = 0};

/* Most recent MSC_SCAN event. We receive scan events before every key event, so
 * we use it for timing comparisons. */
static struct input_event recent_scan;

/* Default output events buffer. */
static struct input_event ev_queue_default[EVENT_BUFFER_SIZE];
size_t ev_queue_default_size = 0;

/* Delayed output events buffer. Events from this buffer are sent strictly after
 * the events from the default event buffer. */
static struct input_event ev_queue_delayed[EVENT_BUFFER_SIZE];
size_t ev_queue_delayed_size = 0;

////////////////////////////////////////////////////////////////////////////////
/// Helper functions

/* Add the event to the default output queue. */
void enqueue_event(const struct input_event *event) {
    check_buffer_not_full(ev_queue_default, ev_queue_default_size);

    ev_queue_default[ev_queue_default_size++] = *event;
}

/* Add the event to the default output queue. Set the time field to that of the
 * recent_scan. */
void enqueue_event_with_recent_time(const struct input_event *event) {
    check_buffer_not_full(ev_queue_default, ev_queue_default_size);

    struct input_event new_event              = *event;
    new_event.time                            = recent_scan.time;
    ev_queue_default[ev_queue_default_size++] = new_event;
}

/* Add the event to the default output queue and add SYN event afterwards. */
void enqueue_event_and_syn(const struct input_event *event) {
    enqueue_event_with_recent_time(event);
    enqueue_event_with_recent_time(&ev_syn);
}

/* Add the event to the delayed output queue. Set the time field to that of the
 * recent_scan. */
void enqueue_delayed_event_with_recent_time(const struct input_event *event) {
    check_buffer_not_full(ev_queue_delayed, ev_queue_delayed_size);

    struct input_event new_event              = *event;
    new_event.time                            = recent_scan.time;
    ev_queue_delayed[ev_queue_delayed_size++] = new_event;
}

/* Add the event to the delayed output queue and add SYN event afterwards. */
void enqueue_delayed_event_and_syn(const struct input_event *event) {
    enqueue_delayed_event_with_recent_time(event);
    enqueue_delayed_event_with_recent_time(&ev_syn);
}

/* Write all events to STDOUT. First the events form the default queue and then
 * from the delayed one. Then set the index variables of both queues to 0. */
void flush_events() {
    if (fwrite(ev_queue_default, sizeof(struct input_event),
               ev_queue_default_size, stdout) != ev_queue_default_size)
        exit(EXIT_FAILURE);

    if (fwrite(ev_queue_delayed, sizeof(struct input_event),
               ev_queue_delayed_size, stdout) != ev_queue_delayed_size)
        exit(EXIT_FAILURE);

    ev_queue_default_size = ev_queue_delayed_size = 0;
}

/* Read next event from STDIN. Return true on success. */
bool read_event(struct input_event *event) {
    return fread(event, sizeof(struct input_event), 1, stdin) == 1;
}

/* Write event to STDOUT. If write failed, exit the program. */
void write_event(const struct input_event *event) {
    if (fwrite(event, sizeof(struct input_event), 1, stdout) != 1)
        exit(EXIT_FAILURE);
}

/* Return the difference in microseconds between the given timevals. */
suseconds_t time_diff(const struct timeval *earlier,
                      const struct timeval *later) {
    return later->tv_usec - earlier->tv_usec +
           (later->tv_sec - earlier->tv_sec) * USEC_PER_SEC;
}

////////////////////////////////////////////////////////////////////////////////
/// Predicates

/* Return true if event is for the given key.
 * Key codes can be found in <input-event-codes.h>. */
bool is_event_for_key(const struct input_event *event, const uint key_code) {
    return event->code == key_code;
}

/* Delay-based guard to protect the key from becoming a modifier too early.
 * This delay is crucial if you type fast enough. */
bool can_lock_to_modifier(const struct timeval *recent_down_time) {
    return time_diff(recent_down_time, &recent_scan.time) >
           (BECOME_MODIFIER_DELAY_MSEC * MSEC_PER_USEC);
}

/* Delay-based guard to prevent insertion of a letter if the key was pressed for
 * a longish time. */
bool can_send_real_down(const struct timeval *recent_down_time) {
    return time_diff(recent_down_time, &recent_scan.time) <
           (REMAIN_REAL_KEY_MSEC * MSEC_PER_USEC);
}

////////////////////////////////////////////////////////////////////////////////
/// Key handlers

void handle_key_down(const struct input_event *event, struct key_state *state) {
    if (is_event_for_key(event, state->key)) {
        if (state->should_hold_modifier_on_key_down) {
            enqueue_delayed_event_and_syn(&state->ev_modifier_down);
            state->is_modifier_held = true;
        }
        state->recent_down_time = event->time;
        state->is_held          = true;
        return;
    }

    // The event is not for the state's key, but if the key is held some
    // magic might need to happen.
    if (state->is_held) {
        if (state->is_locked_to_modifier || state->has_sent_real_down)
            return;

        if (can_lock_to_modifier(&state->recent_down_time)) {
            if (!state->is_modifier_held) {
                enqueue_event_and_syn(&state->ev_modifier_down);
                state->is_modifier_held = true;
            }
            state->is_locked_to_modifier = true;
            return;
        }

        if (can_send_real_down(&state->recent_down_time)) {
            if (state->is_modifier_held) {
                enqueue_event_and_syn(&state->ev_modifier_up);
                state->is_modifier_held = false;
            }
            enqueue_event_and_syn(&state->ev_real_down);
            state->has_sent_real_down = true;
            return;
        }
    }
}

void handle_key_up(const struct input_event *event, struct key_state *state) {
    if (!is_event_for_key(event, state->key))
        return;

    state->is_held = false;

    if (state->is_locked_to_modifier) {
        enqueue_event_and_syn(&state->ev_modifier_up);
        state->is_locked_to_modifier = state->is_modifier_held = false;
        return;
    }

    if (state->is_modifier_held) {
        enqueue_event_and_syn(&state->ev_modifier_up);
        state->is_modifier_held = false;
    }

    if (state->has_sent_real_down) {
        enqueue_event_and_syn(&state->ev_real_up);
        state->has_sent_real_down = false;
        return;
    }

    if (can_send_real_down(&state->recent_down_time)) {
        enqueue_event_and_syn(&state->ev_real_down);
        enqueue_event_and_syn(&state->ev_real_up);
    }
}

bool handle_key(const struct input_event *event, struct key_state *state) {
    if (event->value == KEY_EVENT_DOWN) {
        handle_key_down(event, state);
    } else if (event->value == KEY_EVENT_UP) {
        handle_key_up(event, state);
    }

    return is_event_for_key(event, state->key);
}

////////////////////////////////////////////////////////////////////////////////
/// Entry point

int main(void) {
    struct input_event curr_event;

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

        bool found_handler = false;
        for (size_t i = 0; i < KEY_CONFIG_SIZE; i++) {
            if (handle_key(&curr_event, &key_config[i]))
                found_handler = true;
        }

        if (!found_handler) {
            enqueue_event(&recent_scan);
            enqueue_event(&curr_event);
        }

        flush_events();
    }

    return EXIT_SUCCESS;
}

// Local Variables:
// compile-command: "gcc -Wall -o home-row-fu home-row-fu.c"
// End:
