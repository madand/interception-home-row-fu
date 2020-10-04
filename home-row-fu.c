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

////////////////////////////////////////////////////////////////////////////////
/// Constants

// Microseconds per second
#define USEC_PER_SEC 1e6

// Delay between distinct emulated events. 2e4 us = 20 ms
#define SLEEP_USEC 2e4

#define MODIFIER_FOR_F left_shift

////////////////////////////////////////////////////////////////////////////////
/// Macros

#define SINGLE_EVENT(vname, key, val) \
    struct input_event vname = {      \
        .type = EV_KEY, .code = KEY_##key, .value = val};

#define KEY_EVENTS(vname, key)         \
    SINGLE_EVENT(vname##_down, key, 1) \
    SINGLE_EVENT(vname##_up, key, 0)   \
    SINGLE_EVENT(vname##_repeat, key, 2)

#define KEY_PAIR_EVENTS(vname, name)     \
    KEY_EVENTS(left_##vname, LEFT##name) \
    KEY_EVENTS(right_##vname, RIGHT##name)

// Kludge with double macro to be able to (macro)expand args before
// concatenation.
#define X_EVENT_VAR_NAME(key, event) key##_##event
#define EVENT_VAR_NAME(key, event) X_EVENT_VAR_NAME(key, event)

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

/* Advance event's time by usec microseconds. */
void advance_time(struct input_event *event, suseconds_t usec) {
    event->time.tv_usec = event->time.tv_usec + usec;

    if (event->time.tv_usec >= USEC_PER_SEC) {
        event->time.tv_sec++;
        event->time.tv_usec = event->time.tv_usec - USEC_PER_SEC;
    }
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

/* Copy value of the time from src into dst. */
void copy_time(struct input_event *dst, const struct input_event *src) {
    dst->time = src->time;
}

/* Copy curr_input into recent_scan. */
void save_recent_scan() {
    memcpy(&recent_scan, &curr_input, sizeof(struct input_event));
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

/* Send recent_scan and curr_input without any modifications. */
void passthrough_event() {
    write_event(&recent_scan);
    write_event(&curr_input);
}

/* Send SYN with time copied from the given event.
 * This function should be called right after the event is sent. */
void send_syn_for_event(struct input_event *event) {
    copy_time(&syn, event);
    write_event(&syn);
}

/* Send the given event, with time copied from time_src. Send SYN event
 * immediately afterwards. */
void send_emulated_event(struct input_event *event,
                         struct input_event *time_src) {
    copy_time(event, time_src);
    write_event(event);
    send_syn_for_event(event);
}

////////////////////////////////////////////////////////////////////////////////
/// Key handlers - real magic happens here...

int handle_key_f() {
    static bool is_already_down     = false;
    static bool has_become_modifier = false;
    static struct input_event real_down_event;
    static struct input_event modifier_down_event, modifier_up_event;

    // Initialize helper vars on the first invocation.
    if (modifier_down_event.type != EV_KEY) {
        modifier_down_event = EVENT_VAR_NAME(MODIFIER_FOR_F, down);
    }
    if (modifier_up_event.type != EV_KEY) {
        modifier_up_event = EVENT_VAR_NAME(MODIFIER_FOR_F, up);
    }

    // Trash all Repeat events for the target key.
    if (is_key(KEY_F) && is_key_repeat_event())
        return true;

    // Bail out early if the target key is not down and the current event is not
    // about the target key.
    if (!is_key(KEY_F) && !is_already_down)
        return false;

    // Current key is NOT F and F is already Down.
    if (!is_key(KEY_F)) {
        if (!has_become_modifier) {
            // From this point on the target key "becomes" a modifier, until its
            // Up event arrives.

            send_emulated_event(&modifier_down_event, &recent_scan);

            usleep(SLEEP_USEC);
            advance_time(&curr_input, SLEEP_USEC);
            send_emulated_event(&curr_input, &curr_input);

            has_become_modifier = true;
        }

        return true;
    }

    // After this point we know that event is about the target key, and its type
    // is either Key Up or Key Down. (Repeat events are filtered out near the
    // beginning of this function).

    if (is_key_down_event()) {
        // Remember that target keys is Down, stop handling without emitting any
        // events.
        is_already_down = true;

        return true;
    }

    // After this point we know that it's a Key Up event and key is (obviously)
    // already Down. The only unknown here is whether it became a modifier.

    if (has_become_modifier) {
        send_emulated_event(&modifier_up_event, &recent_scan);
    } else {
        passthrough_event();
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
            save_recent_scan();
            continue;
        }

        if (curr_input.type != EV_KEY) {
            write_event(&curr_input);
            continue;
        }

        if (handle_key_f())
            continue;

        // Pass through other EV_KEY events along with their MSC_SCAN.
        passthrough_event();
    }

    return 0;
}

// Local Variables:
// compile-command: "gcc -o home-row-fu home-row-fu.c"
// End:
