// home-row-fu.c

// Author: Andriy B. Kmit' <dev@madand.net>
// Released into Public Domain under CC0:
// https://creativecommons.org/publicdomain/zero/1.0/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <linux/input.h>
#include <unistd.h>

#include "macros.h"

////////////////////////////////////////////////////////////////////////////////
/// Key event prototype variables declarations

// clang-format off
// Generate series of var definitions for keys we will work with. For each key
// six definitions are generated, three for left and three for the right
// counterpart.
// E.g. KEY_PAIR_EVENTS(shift, SHIFT) expands to declarations of the following
// vars:
//   left_shift_down, left_shift_up, left_shift_repeat,
//   right_shift_down, right_shift_up, right_shift_repeat.
// Where each var declaration is of the form:
//   struct input_event left_shift_down = { .type = EV_KEY, .code = KEY_LEFTSHIFT, .value = 1};
KEY_PAIR_EVENTS(shift, SHIFT);
KEY_PAIR_EVENTS(ctrl, CTRL);
KEY_PAIR_EVENTS(alt, ALT);
KEY_PAIR_EVENTS(meta, META);

// Key pairs for modifier emulation.
// E.g. A -> Left Shift; SEMICOLON -> Right Shift
KEY_EVENTS(a, A); KEY_EVENTS(semicolon, SEMICOLON);
KEY_EVENTS(s, S); KEY_EVENTS(l, L);
KEY_EVENTS(f, F); KEY_EVENTS(j, J);
KEY_EVENTS(d, D); KEY_EVENTS(k, K);
// clang-format on

/* SYN event should be sent after each emulated event. */
struct input_event syn = {.type = EV_SYN, .code = SYN_REPORT, .value = 0};

////////////////////////////////////////////////////////////////////////////////
/// Global state

/* Event currently being processed by the main loop. */
struct input_event curr_input;
/* Most recent MSC_SCAN event. */
struct input_event recent_scan;

////////////////////////////////////////////////////////////////////////////////
/// Helper functions

/* Read next event from STDIN. Return 1 on success. */
int read_event(struct input_event *event) {
    return fread(event, sizeof(struct input_event), 1, stdin) == 1;
}

/* Write event to STDOUT. If write failed, exit the program. */
void write_event(const struct input_event *event) {
    if (fwrite(event, sizeof(struct input_event), 1, stdout) != 1)
        exit(EXIT_FAILURE);
}

/* Copy value of the time from src into dst. */
void copy_time(struct input_event *dst, const struct input_event *src) {
    dst->time = src->time;
}

/* Return 1 if both arguments have equal values of members type, code and value.
 * Value of the time member is not considered by this function. */
bool equal(const struct input_event *first, const struct input_event *second) {
    return first->type == second->type && first->code == second->code &&
           first->value == second->value;
}

/* Return true if curr_input is Key Down event. */
bool is_key_down_event() { return curr_input.value == 1; }

/* Return true if curr_input is Key Up event. */
bool is_key_up_event() { return curr_input.value == 0; }

/* Return true if curr_input is Key Repeat event. */
bool is_key_repeat_event() { return curr_input.value == 2; }

/* Return true if curr_input.code is equal to the given key. */
bool is_key(const uint key) { return curr_input.code == key; }

/* Send SYN with time copied from the given event.
 * This function should be called right after the event is sent. */
void send_syn_for_event(struct input_event *event) {
    copy_time(&syn, event);
    write_event(&syn);
}

/* Send recent_scan and curr_input without any modifications. */
void send_original_event() {
    write_event(&recent_scan);
    write_event(&curr_input);
    // No need to manually send SYN here, since we just letting through the real
    // event, the next input event will be its SYN anyway.
}

// Send the given event, with time copied from recent_scan. Send SYN event
// immediately afterwards.
void send_emulated_event(struct input_event *event) {
    copy_time(event, &recent_scan);
    write_event(event);
    send_syn_for_event(event);
}

////////////////////////////////////////////////////////////////////////////////
/// Key handlers - all magic happens here...

int handle_key_f() {
    static bool is_already_down     = false;
    static bool has_became_modifier = false;
    static struct input_event down_scan, down_event;

    // Trash all Repeat events for the target key.
    if (is_key(KEY_F) && is_key_repeat_event())
        return true;

    // Bail out early if target key is not down and current event is about
    // a different key.
    if (!is_key(KEY_F) && !is_already_down)
        return false;

    if (is_key(KEY_F)) {
        if (is_already_down) {
            if (is_key_up_event()) {
                if (has_became_modifier) {
                    send_emulated_event(EVENT_VAR_NAME(MODIFIER_FOR_F, up));
                } else {
                    send_original_event();
                }
            }
        }
    }

    // Being here means: current key is NOT F and F is already Down.
    if (!has_became_modifier) {
        has_became_modifier = true;
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////
/// Entry point

int main(void) {
    struct input_event curr_input, recent_scan;

    setbuf(stdin, NULL), setbuf(stdout, NULL);

    while (read_event(&curr_input)) {
        if (curr_input.type == EV_MSC && curr_input.code == MSC_SCAN) {
            memcpy(&recent_scan, &curr_input, sizeof(struct input_event));
            continue;
        }

        if (curr_input.type != EV_KEY) {
            write_event(&curr_input);
            continue;
        }

        if (handle_key_f())
            continue;

        // Pass through other EV_KEY events along with their MSC_SCAN.
        write_event(&recent_scan);
        write_event(&curr_input);
    }

    return 0;
}
