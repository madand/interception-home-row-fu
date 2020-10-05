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

// Delay when sending two events. 2e4 microseconds = 20 milliseconds.
#define SLEEP_USEC 2e4

// Delay (in milliseconds) before key can become a modifier.
#define BECOME_MODIFIER_DELAY_MS 150
// Delay (in microseconds) before key can become a modifier.
#define BECOME_MODIFIER_DELAY_USEC (BECOME_MODIFIER_DELAY_MS * 1e3)

#define MODIFIER_FOR_F left_shift
#define MODIFIER_FOR_J right_shift

#define EVENT_NOT_HANDLED false
#define EVENT_HANDLED true

////////////////////////////////////////////////////////////////////////////////
/// Macros

#define DEFINE_EVENT_VAR(vname, key, val) \
    const struct input_event vname = {    \
        .type = EV_KEY, .code = KEY_##key, .value = val};

#define KEY_EVENTS(vname, key)             \
    DEFINE_EVENT_VAR(vname##_down, key, 1) \
    DEFINE_EVENT_VAR(vname##_up, key, 0)   \
    DEFINE_EVENT_VAR(vname##_repeat, key, 2)

#define KEY_PAIR_EVENTS(vname, name)     \
    KEY_EVENTS(left_##vname, LEFT##name) \
    KEY_EVENTS(right_##vname, RIGHT##name)

#define HANDLE_KEY_CALL(event, key, modifier) \
    handle_key(event, KEY_##key, &modifier##_down, &modifier##_up)

#define HANDLE_KEY_STATEMENT(event, key, modifier)              \
    if (HANDLE_KEY_CALL(event, key, modifier) == EVENT_HANDLED) \
    continue

#define HANDLE_KEY_PAIR(event, left_key, right_key, modifier) \
    HANDLE_KEY_STATEMENT(event, left_key, left_##modifier);   \
    HANDLE_KEY_STATEMENT(event, right_key, right_##modifier);

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
// clang-format on

/* SYN event should be sent after each emulated event. */
const struct input_event syn = {.type = EV_SYN, .code = SYN_REPORT, .value = 0};

////////////////////////////////////////////////////////////////////////////////
/// Global state

/* Most recent MSC_SCAN event. */
struct input_event recent_scan;

/* State that handle_key() needs to keep between calls. */
bool is_already_down[KEY_MAX], has_became_modifier[KEY_MAX],
    has_sent_real_down[KEY_MAX];

struct input_event real_down_event[KEY_MAX];

////////////////////////////////////////////////////////////////////////////////
/// Helper functions

/* Advance the given time by usec microseconds. */
struct timeval advance_time(const struct timeval time, suseconds_t usec) {
    struct timeval new_time = time;

    new_time.tv_usec = new_time.tv_usec + usec;

    if (new_time.tv_usec >= USEC_PER_SEC) {
        new_time.tv_sec++;
        new_time.tv_usec = new_time.tv_usec - USEC_PER_SEC;
    }

    return new_time;
}

/* Return the time difference between two events. The return value is in
 * microseconds. */
suseconds_t event_time_diff(const struct input_event *earlier,
                            const struct input_event *later) {
    return later->time.tv_usec - earlier->time.tv_usec +
           (later->time.tv_sec - earlier->time.tv_sec) * USEC_PER_SEC;
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

/* Write the given event to STDOUT, with the given time. */
void write_event_with_time(const struct input_event *event,
                           const struct timeval time) {
    struct input_event tmp_event = *event;

    tmp_event.time = time;
    write_event(&tmp_event);
}

/* Send the given event. */
void send_event(const struct input_event *event) {
    struct timeval time = recent_scan.time;

    write_event_with_time(event, time);
    write_event_with_time(&syn, time);
}

/* Send the given events with short delay between them. */
void send_events_with_delay(const struct input_event *event1,
                            const struct input_event *event2) {
    struct timeval time1 = recent_scan.time,
                   time2 = advance_time(time1, (suseconds_t)SLEEP_USEC);

    write_event_with_time(event1, time1);
    write_event_with_time(&syn, time1);

    usleep(SLEEP_USEC);

    write_event_with_time(event2, time2);
    write_event_with_time(&syn, time2);
}

/* Return 1 if both arguments have equal values of members type, code and value.
 * Value of the time member is not considered by this function. */
bool equal(const struct input_event *first, const struct input_event *second) {
    return first->type == second->type && first->code == second->code &&
           first->value == second->value;
}

/* Return true if event is Key Down event. */
bool is_key_down_event(const struct input_event *event) {
    return event->value == 1;
}

/* Return true if event is Key Up event. */
bool is_key_up_event(const struct input_event *event) {
    return event->value == 0;
}

/* Return true if event is Key Repeat event. */
bool is_key_repeat_event(const struct input_event *event) {
    return event->value == 2;
}

/* Return true if event is for the given key.
 * Key codes can be found in <input-event-codes.h>. */
bool is_event_for_key(const struct input_event *event, const uint key_code) {
    return event->code == key_code;
}

/* Delay-based guard to protect the key from becoming a modifier too early.
 * This delay is crucial if you type fast enough. */
bool can_become_modifier(const struct input_event *real_key_down_event) {
    return event_time_diff(real_key_down_event, &recent_scan) >
           BECOME_MODIFIER_DELAY_USEC;
}

////////////////////////////////////////////////////////////////////////////////

/* Key handler - real magic happens here... */
bool handle_key(const struct input_event *event, const uint key,
                const struct input_event *modifier_down_event,
                const struct input_event *modifier_up_event) {
    // Skip all Repeat events for the target key.
    if (is_event_for_key(event, key) && is_key_repeat_event(event))
        return EVENT_HANDLED;

    if (!is_event_for_key(event, key) && !is_already_down[key])
        return EVENT_NOT_HANDLED;

    // Current key is NOT F and F is already Down.
    if (!is_event_for_key(event, key)) {
        if (has_became_modifier[key] || has_sent_real_down[key])
            return EVENT_NOT_HANDLED;

        if (can_become_modifier(&real_down_event[key])) {
            send_events_with_delay(modifier_down_event, event);
            has_became_modifier[key] = true;

            return EVENT_HANDLED;
        }

        send_events_with_delay(&real_down_event[key], event);
        has_sent_real_down[key] = true;

        return EVENT_HANDLED;
    }

    // After this point we know that event is about the target key, and its type
    // is either Key Up or Key Down (Key Repeat events are filtered out near the
    // function's beginning).

    if (is_key_down_event(event)) {
        real_down_event[key] = *event;
        is_already_down[key] = true;

        return EVENT_HANDLED;
    }

    // After this point we know that it's a Key Up event.

    is_already_down[key] = false;

    if (has_became_modifier[key]) {
        send_event(modifier_up_event);
        has_became_modifier[key] = false;

        return EVENT_HANDLED;
    }

    if (has_sent_real_down[key]) {
        send_event(event);
        has_sent_real_down[key] = false;

        return EVENT_HANDLED;
    }

    send_events_with_delay(&real_down_event[key], event);

    return EVENT_HANDLED;
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

        HANDLE_KEY_PAIR(&curr_event, F, J, ctrl);
        HANDLE_KEY_PAIR(&curr_event, D, K, meta);
        HANDLE_KEY_PAIR(&curr_event, S, L, shift);

        // If no handler handled this event, pass it through along with its
        // MSC_SCAN.
        write_event(&recent_scan);
        write_event(&curr_event);
    }

    return EXIT_SUCCESS;
}

// Local Variables:
// compile-command: "gcc -Wall -o home-row-fu home-row-fu.c"
// End:
