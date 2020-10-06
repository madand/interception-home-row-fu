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
#define BECOME_MODIFIER_DELAY_MSEC 150
// Delay (in microseconds) before key can become a modifier.
#define BECOME_MODIFIER_DELAY_USEC (BECOME_MODIFIER_DELAY_MSEC * 1e3)

// Delay (in milliseconds) before key can become a modifier.
#define REMAIN_REAL_KEY_MSEC 1000
// Delay (in microseconds) before key can become a modifier.
#define REMAIN_REAL_KEY_USEC (REMAIN_REAL_KEY_MSEC * 1e3)

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

#define HANDLE_KEY_CALL(key, modifier) \
    handle_key(&curr_event, key, &modifier##_down, &modifier##_up)

#define HANDLE_KEY_STATEMENT(key, modifier) \
    found_handler = found_handler || HANDLE_KEY_CALL(key, modifier);

#define HANDLE_KEY_PAIR(left_key, right_key, modifier) \
    HANDLE_KEY_STATEMENT(left_key, left_##modifier);   \
    HANDLE_KEY_STATEMENT(right_key, right_##modifier);

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
static struct input_event recent_scan;
/* Variable for keeping time value when sending emulatied events. recent_time
 * gets advanced by small mount after each send_event() and gets synchronized
 * with recent_scan on each new SCAN event. */
static struct timeval recent_time;

/* State that handle_key() needs to keep between calls. */
static char is_already_down[KEY_MAX], has_became_modifier[KEY_MAX],
    has_sent_real_down[KEY_MAX];

static struct input_event recent_down_event[KEY_MAX];

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

void advance_recent_time(suseconds_t usec) {
    recent_time = advance_time(recent_time, usec);
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
    write_event_with_time(event, recent_time);
    write_event_with_time(&syn, recent_time);

    advance_recent_time(SLEEP_USEC);
    usleep(SLEEP_USEC);
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

bool can_send_real_down(const struct input_event *real_key_down_event) {
    return event_time_diff(real_key_down_event, &recent_scan) <
           REMAIN_REAL_KEY_USEC;
}

////////////////////////////////////////////////////////////////////////////////

void handle_key_down(const struct input_event *event, const uint key,
                     const struct input_event *modifier_down_event,
                     const struct input_event *modifier_up_event) {
    if (!is_event_for_key(event, key) && is_already_down[key]) {
        if (has_became_modifier[key] || has_sent_real_down[key])
            return;

        if (can_become_modifier(&recent_down_event[key])) {
            send_event(modifier_down_event);
            has_became_modifier[key] = true;

            return;
        }

        if (can_send_real_down(&recent_down_event[key])) {
            send_event(&recent_down_event[key]);
            has_sent_real_down[key] = true;

            return;
        }
    }

    if (is_event_for_key(event, key)) {
        recent_down_event[key] = *event;
        is_already_down[key]   = true;
    }
}

void handle_key_up(const struct input_event *event, const uint key,
                   const struct input_event *modifier_down_event,
                   const struct input_event *modifier_up_event) {
    if (!is_event_for_key(event, key)) {
        return;
    }

    is_already_down[key] = false;

    if (has_became_modifier[key]) {
        send_event(modifier_up_event);
        has_became_modifier[key] = false;

        return;
    }

    if (has_sent_real_down[key]) {
        send_event(event);
        has_sent_real_down[key] = false;

        return;
    }

    if (can_send_real_down(&recent_down_event[key])) {
        send_event(&recent_down_event[key]);
        send_event(event);
    }
}

/* Key handler - real magic happens here... */
bool handle_key(const struct input_event *event, const uint key,
                const struct input_event *modifier_down_event,
                const struct input_event *modifier_up_event) {
    if (is_key_down_event(event)) {
        handle_key_down(event, key, modifier_down_event, modifier_up_event);
    } else if (is_key_up_event(event)) {
        handle_key_up(event, key, modifier_down_event, modifier_up_event);
    } else {
        // We never handle other event types.
        return false;
    }

    return is_event_for_key(event, key);
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
            recent_time = curr_event.time;
            continue;
        }

        if (curr_event.type != EV_KEY) {
            write_event(&curr_event);
            continue;
        }

        if (is_key_repeat_event(&curr_event)) {
            continue;
        }

        found_handler = false;

        // Modify these to change keys<->modifier correspondence to your
        // taste. Available key constants are listed in input-event-codes.h
        // Available modifiers: shift, alt, ctrl, meta. Meta is a
        // Windows-key on most keyboards.
        HANDLE_KEY_PAIR(KEY_F, KEY_J, ctrl);
        HANDLE_KEY_PAIR(KEY_D, KEY_K, meta);
        HANDLE_KEY_PAIR(KEY_S, KEY_L, shift);
        HANDLE_KEY_PAIR(KEY_A, KEY_SEMICOLON, alt);

        // If no handler handled this event, pass it through along with its
        // MSC_SCAN.
        if (!found_handler) {
            write_event_with_time(&recent_scan, recent_time);
            write_event_with_time(&curr_event, recent_time);
        }
    }

    return EXIT_SUCCESS;
}

// Local Variables:
// compile-command: "gcc -Wall -o home-row-fu home-row-fu.c"
// End:
