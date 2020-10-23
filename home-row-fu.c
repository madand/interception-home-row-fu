// home-row-fu.c

// Author: Andriy B. Kmit' <dev@madand.net>
// Public Domain / CC0 https://creativecommons.org/publicdomain/zero/1.0/

#include <stdio.h>        // setbuf, fwrite, fread
#include <stdlib.h>       // EXIT_SUCCESS, EXIT_FAILURE
#include <stdbool.h>      // bool, true, false
#include <stdint.h>       // size_t
#include <assert.h>       // assert
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
 * input_event). */
#define EVENT_BUFFER_SIZE 20

////////////////////////////////////////////////////////////////////////////////
/// Global state

/* SYN event should be sent after each emulated event. */
static const struct input_event ev_syn = {
    .type = EV_SYN, .code = SYN_REPORT, .value = 0};

/* Most recent MSC_SCAN event. We receive scan evens before every key event, so
 * we use it for timing comparisons. */
static struct input_event recent_scan;

/* Default output events buffer. */
static struct input_event ev_queue_default[EVENT_BUFFER_SIZE];
size_t ev_queue_default_size = 0;

/* Delayed output events buffer. Evens from this buffer are sent strictly after
 * the events from the default event buffer. */
static struct input_event ev_queue_delayed[EVENT_BUFFER_SIZE];
size_t ev_queue_delayed_size = 0;

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
    // Down and Up events conveniently prepared for sending when the time comes:
    const struct input_event ev_real_down;
    const struct input_event ev_real_up;
    const struct input_event ev_modifier_down;
    const struct input_event ev_modifier_up;
} key_config[] = {
#define DEFINE_MAPPING(key_code, modifier_code)        \
    {                                                  \
        .key              = key_code,                  \
        .ev_real_down     = {.type  = EV_KEY,          \
                         .code  = key_code,        \
                         .value = KEY_EVENT_DOWN}, \
        .ev_real_up       = {.type  = EV_KEY,          \
                       .code  = key_code,        \
                       .value = KEY_EVENT_UP},   \
        .ev_modifier_down = {.type  = EV_KEY,          \
                             .code  = modifier_code,   \
                             .value = KEY_EVENT_DOWN}, \
        .ev_modifier_up   = {.type  = EV_KEY,          \
                           .code  = modifier_code,   \
                           .value = KEY_EVENT_UP},   \
    },

// Edit key_config.h to change the mappings to your taste.
#include "key_config.h"

#undef DEFINE_MAPPING
};

static const size_t key_config_size =
    sizeof(key_config) / sizeof(key_config[0]);

////////////////////////////////////////////////////////////////////////////////
/// Helper functions

/* Add the event to the default output queue. */
void enqueue_event(const struct input_event *event) {
    ev_queue_delayed[ev_queue_delayed_size++] = *event;
    assert(ev_queue_delayed_size <= EVENT_BUFFER_SIZE);
}

/* Add the event to the default output queue and add SYN event afterwards. */
void enqueue_event_and_syn(const struct input_event *event) {
    ev_queue_default[ev_queue_default_size++] = *event;
    assert(ev_queue_default_size <= EVENT_BUFFER_SIZE);

    ev_queue_default[ev_queue_default_size++] = ev_syn;
    assert(ev_queue_default_size <= EVENT_BUFFER_SIZE);
}

/* Add the event to the delayed output queue and add SYN event afterwards. */
void enqueue_delayed_event_and_syn(const struct input_event *event) {
    ev_queue_delayed[ev_queue_delayed_size++] = *event;
    assert(ev_queue_delayed_size <= EVENT_BUFFER_SIZE);

    ev_queue_delayed[ev_queue_delayed_size++] = ev_syn;
    assert(ev_queue_delayed_size <= EVENT_BUFFER_SIZE);
}

/* Write all events to STDOUT. First the events form the default queue and then
 * from the delayed one. Then set the index variables of both queues to 0. */
void flush_events() {
    bool has_merged_into_default_queue = false;

    // In order to avoid 2 write() syscalls, attempt to merge all events into
    // ev_queue_modifiers if size permits.
    if ((ev_queue_default_size + ev_queue_delayed_size) <=
        2 * EVENT_BUFFER_SIZE) {
        // Copy events form the default queue over to the modifiers queue.
        for (size_t i = 0; i < ev_queue_delayed_size; i++) {
            ev_queue_default[ev_queue_default_size++] = ev_queue_delayed[i];
        }
        has_merged_into_default_queue = true;
    }

    if (fwrite(ev_queue_default, sizeof(struct input_event),
               ev_queue_default_size, stdout) != ev_queue_default_size)
        exit(EXIT_FAILURE);

    if (!has_merged_into_default_queue) {
        // Queues are too big to be merged into one, so we must write out the
        // delayed queue as a separate step.
        if (fwrite(ev_queue_delayed, sizeof(struct input_event),
                   ev_queue_delayed_size, stdout) != ev_queue_delayed_size)
            exit(EXIT_FAILURE);
    }

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
bool can_lock_to_modifier(const struct timeval *recent_down_time) {
    return time_diff(recent_down_time, &recent_scan.time) >
           (BECOME_MODIFIER_DELAY_MSEC * MSEC_PER_USEC);
}

/* Delay-based guard to protect the key from inserting a letter if pressed for a
 * longish time. */
bool can_send_real_down(const struct timeval *recent_down_time) {
    return time_diff(recent_down_time, &recent_scan.time) <
           (REMAIN_REAL_KEY_MSEC * MSEC_PER_USEC);
}

////////////////////////////////////////////////////////////////////////////////
/// Key handlers

void handle_key_down(const struct input_event *event, struct key_state *state) {
    if (is_event_for_key(event, state->key)) {
        enqueue_delayed_event_and_syn(&state->ev_modifier_down);
        state->recent_down_time = event->time;
        state->is_held = state->is_modifier_held = true;
        return;
    }

    // The event is not for the state's key, but if the key is held some
    // magic might need to happen.
    if (state->is_held) {
        if (state->is_locked_to_modifier || state->has_sent_real_down)
            return;

        if (can_lock_to_modifier(&state->recent_down_time)) {
            state->is_locked_to_modifier = true;
            return;
        }

        if (can_send_real_down(&state->recent_down_time)) {
            enqueue_event_and_syn(&state->ev_modifier_up);
            state->is_modifier_held = false;

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

        flush_events();
    }

    return EXIT_SUCCESS;
}

// Local Variables:
// compile-command: "gcc -Wall -o home-row-fu home-row-fu.c"
// End:
