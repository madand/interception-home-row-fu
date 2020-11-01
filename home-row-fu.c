// home-row-fu.c

// Author: Andriy B. Kmit' <dev@madand.net>
// URL: https://github.com/madand/interception-home-row-fu
// Public Domain / CC0 https://creativecommons.org/publicdomain/zero/1.0/

#include <stdio.h>   // setbuf, fwrite, fread
#include <stdlib.h>  // EXIT_SUCCESS, EXIT_FAILURE
#include <stdbool.h>
#include <stdint.h>
#include <linux/input.h>  // struct input_event, KEY_A ...
#include <libevdev/libevdev.h>
#include <toml.h>

#include "home-row-fu.h"

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
    input_event ev_real_down;
    input_event ev_real_up;
    input_event ev_modifier_down;
    input_event ev_modifier_up;
};

////////////////////////////////////////////////////////////////////////////////
/// Global state

/* SYN event should be sent after each emulated event. */
static const input_event ev_syn = {
    .type = EV_SYN, .code = SYN_REPORT, .value = 0};

/* Most recent MSC_SCAN event. We receive scan events before every key event, so
 * we use it for timing comparisons. */
static input_event recent_scan;

/* Default output events buffer. */
static input_event ev_queue_default[EVENT_BUFFER_SIZE];
size_t ev_queue_default_size = 0;

/* Delayed output events buffer. Events from this buffer are sent strictly after
 * the events from the default event buffer. */
static input_event ev_queue_delayed[EVENT_BUFFER_SIZE];
size_t ev_queue_delayed_size = 0;

static int64_t burst_typing_msec      = DEFAULT_BURST_TYPING_MSEC,
               can_insert_letter_msec = DEFAULT_CAN_INSERT_LETTER_MSEC;

static key_state *mappings;
static int mappings_size = 0;

////////////////////////////////////////////////////////////////////////////////
/// Helper functions

/* Add the event to the default output queue. */
static inline void enqueue_event(const input_event *event) {
    check_buffer_not_full(ev_queue_default, ev_queue_default_size);

    ev_queue_default[ev_queue_default_size++] = *event;
}

/* Add the event to the default output queue. Set the time field to that of the
 * recent_scan. */
static inline void enqueue_event_with_recent_time(const input_event *event) {
    check_buffer_not_full(ev_queue_default, ev_queue_default_size);

    input_event new_event                     = *event;
    new_event.time                            = recent_scan.time;
    ev_queue_default[ev_queue_default_size++] = new_event;
}

/* Add the event to the default output queue and add SYN event afterwards. */
static inline void enqueue_event_and_syn(const input_event *event) {
    enqueue_event_with_recent_time(event);
    enqueue_event_with_recent_time(&ev_syn);
}

/* Add the event to the delayed output queue. Set the time field to that of the
 * recent_scan. */
static inline void enqueue_delayed_event_with_recent_time(
    const input_event *event) {
    check_buffer_not_full(ev_queue_delayed, ev_queue_delayed_size);

    input_event new_event                     = *event;
    new_event.time                            = recent_scan.time;
    ev_queue_delayed[ev_queue_delayed_size++] = new_event;
}

/* Add the event to the delayed output queue and add SYN event afterwards. */
static inline void enqueue_delayed_event_and_syn(const input_event *event) {
    enqueue_delayed_event_with_recent_time(event);
    enqueue_delayed_event_with_recent_time(&ev_syn);
}

/* Write all events to STDOUT. First the events form the default queue and then
 * from the delayed one. Then set the index variables of both queues to 0. */
static inline void flush_events() {
    if (fwrite(ev_queue_default, sizeof(input_event), ev_queue_default_size,
               stdout) != ev_queue_default_size)
        exit(EXIT_FAILURE);

    if (fwrite(ev_queue_delayed, sizeof(input_event), ev_queue_delayed_size,
               stdout) != ev_queue_delayed_size)
        exit(EXIT_FAILURE);

    ev_queue_default_size = ev_queue_delayed_size = 0;
}

/* Read next event from STDIN. Return true on success. */
static inline bool read_event(input_event *event) {
    return fread(event, sizeof(input_event), 1, stdin) == 1;
}

/* Write event to STDOUT. If write failed, exit the program. */
static inline void write_event(const input_event *event) {
    if (fwrite(event, sizeof(input_event), 1, stdout) != 1)
        exit(EXIT_FAILURE);
}

/* Return the difference in microseconds between the given timevals. */
static inline suseconds_t time_diff(const struct timeval *earlier,
                                    const struct timeval *later) {
    return later->tv_usec - earlier->tv_usec +
           (later->tv_sec - earlier->tv_sec) * USEC_PER_SEC;
}

////////////////////////////////////////////////////////////////////////////////
/// Predicates

/* Return true if event is for the given key.
 * Key codes can be found in <input-event-codes.h>. */
static inline bool is_event_for_key(const input_event *event,
                                    const uint16_t key_code) {
    return event->code == key_code;
}

/* Delay-based guard to protect the key from becoming a modifier too early.
 * This delay is crucial if you type fast enough. */
static inline bool can_lock_to_modifier(
    const struct timeval *recent_down_time) {
    return time_diff(recent_down_time, &recent_scan.time) >
           (burst_typing_msec * MSEC_PER_USEC);
}

/* Delay-based guard to prevent insertion of a letter if the key was pressed for
 * a longish time. */
static inline bool can_send_real_down(const struct timeval *recent_down_time) {
    return time_diff(recent_down_time, &recent_scan.time) <
           (can_insert_letter_msec * MSEC_PER_USEC);
}

////////////////////////////////////////////////////////////////////////////////
/// Key handlers

static inline void handle_key_down(const input_event *event, key_state *state) {
    if (is_event_for_key(event, state->key)) {
        if (state->simulate_modifier_press_on_key_down) {
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

static inline void handle_key_up(const input_event *event, key_state *state) {
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

static inline bool handle_key(const input_event *event, key_state *state) {
    if (event->value == EVENT_VALUE_KEY_DOWN) {
        handle_key_down(event, state);
    } else if (event->value == EVENT_VALUE_KEY_UP) {
        handle_key_up(event, state);
    }

    return is_event_for_key(event, state->key);
}

////////////////////////////////////////////////////////////////////////////////
/// Configuration handling

static void read_config_int(const toml_table_t *table, const char *key,
                            int64_t *ret) {
    int64_t maybe_ret;
    toml_raw_t currval = toml_raw_in(table, key);
    if (currval != NULL && toml_rtoi(currval, &maybe_ret) != -1) {
        if (maybe_ret >= 0) {
            *ret = maybe_ret;
        } else {
            fprintf(stderr, "Warning: ignoring negative value (%ld) for %s\n",
                    maybe_ret, key);
        }
    }
}

static void read_config_bool(const toml_table_t *table, const char *key,
                             bool default_ret, bool *ret) {
    int maybe_ret;
    toml_raw_t currval = toml_raw_in(table, key);

    if (currval != NULL && toml_rtob(currval, &maybe_ret) != -1) {
        *ret = maybe_ret;
    } else {
        *ret = default_ret;
    }
}

static void read_config_key_code(const toml_table_t *table, const char *key,
                                 uint16_t *ret) {
    int64_t maybe_ret;
    toml_raw_t currval = toml_raw_in(table, key);

    if (currval == NULL) {
        fprintf(stderr, "Error: %s is not set.\n", key);
        exit(EXIT_FAILURE);
    }

    // First try to read it as int
    if (toml_rtoi(currval, &maybe_ret) != -1) {
        if (maybe_ret >= 0) {
            *ret = (uint16_t)maybe_ret;
            return;
        } else {
            fprintf(stderr, "Error: %s is negative.\n", key);
            exit(EXIT_FAILURE);
        }
    }

    // If not int, it might be a string key code name.
    char *key_code_str;
    if (toml_rtos(currval, &key_code_str) != -1) {
        maybe_ret = libevdev_event_code_from_name(EV_KEY, key_code_str);
        if (maybe_ret >= 0) {
            *ret = (uint16_t)maybe_ret;
            free(key_code_str);
            return;
        } else {
            fprintf(stderr, "Error: unknown key name %s\n", key_code_str);
            exit(EXIT_FAILURE);
        }
    }

    fprintf(stderr, "Error: unknown value of %s. Must be integer or string.\n",
            key);
    exit(EXIT_FAILURE);
}

static void init_single_mapping(key_state *mapping, uint16_t key_code,
                                uint16_t modifier_code,
                                bool simulate_modifier_press_on_key_down) {
    // clang-format off
    *mapping = (key_state){
        .key = key_code,
        .simulate_modifier_press_on_key_down = simulate_modifier_press_on_key_down,
        .ev_real_down     = {
            .type  = EV_KEY,
            .code  = key_code,
            .value = EVENT_VALUE_KEY_DOWN
        },
        .ev_real_up       = {
            .type  = EV_KEY,
            .code  = key_code,
            .value = EVENT_VALUE_KEY_UP
        },
        .ev_modifier_down = {
            .type  = EV_KEY,
            .code  = modifier_code,
            .value = EVENT_VALUE_KEY_DOWN
        },
        .ev_modifier_up   = {
            .type  = EV_KEY,
            .code  = modifier_code,
            .value = EVENT_VALUE_KEY_UP
        },
    };
    // clang-format on
}

static void read_config_mapping(key_state *mapping, const toml_table_t *table) {
    uint16_t physical_key_code, modifier_key_code;
    bool simulate_modifier_press_on_key_down;

    read_config_key_code(table, "physical_key", &physical_key_code);
    read_config_key_code(table, "modifier_key", &modifier_key_code);
    read_config_bool(table, "simulate_modifier_press_on_key_down", true,
                     &simulate_modifier_press_on_key_down);

    init_single_mapping(mapping, physical_key_code, modifier_key_code,
                        simulate_modifier_press_on_key_down);
}

static void read_config_mappings(const toml_table_t *table) {
    toml_array_t *marr;

    marr = toml_array_in(table, "mapping");
    if (marr == NULL || (mappings_size = toml_array_nelem(marr)) == 0) {
        fprintf(stderr,
                "Warning: no mappings found in the config file.\n"
                "The plugin will work as no-op!\n");
        return;
    }

    mappings = calloc(sizeof(*mappings), mappings_size);
    if (mappings == NULL) {
        fprintf(stderr, "Failed to allocate memory!\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < mappings_size; i++)
        read_config_mapping(mappings + i, toml_table_at(marr, i));
}

static void load_config(const char *config_file) {
    FILE *fp;
    char err_buf[TOML_ERROR_BUFFER_SIZE];
    toml_table_t *table;

    fp = fopen(config_file, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to open config file: %s\n", config_file);
        exit(EXIT_FAILURE);
    }

    table = toml_parse_file(fp, err_buf, TOML_ERROR_BUFFER_SIZE);
    fclose(fp);

    if (table == NULL) {
        fprintf(stderr, "Failed to parse config file: %s\nError: %s\n",
                config_file, err_buf);
        exit(EXIT_FAILURE);
    }

    read_config_int(table, "burst_typing_msec", &burst_typing_msec);
    read_config_int(table, "can_insert_letter_msec", &can_insert_letter_msec);

    read_config_mappings(table);

    toml_free(table);
}

////////////////////////////////////////////////////////////////////////////////
/// Entry point

int main() {
    input_event curr_event;

    setbuf(stdin, NULL), setbuf(stdout, NULL);

    // TODO: read config file name as cli param
    load_config(DEFAULT_CONFIG_FILE);

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
        for (int i = 0; i < mappings_size; i++) {
            if (handle_key(&curr_event, &mappings[i]))
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
// compile-command: "meson compile -C build"
// End:
