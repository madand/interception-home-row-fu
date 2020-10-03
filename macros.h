// macros.h

// Author: Andriy B. Kmit' <dev@madand.net>
// Released into Public Domain
// See CC0: https://creativecommons.org/publicdomain/zero/1.0/

#define SINGLE_EVENT(vname, key, val) \
    struct input_event vname = {      \
        .type = EV_KEY, .code = KEY_##key, .value = val};

#define KEY_EVENTS(vname, key)         \
    SINGLE_EVENT(vname##_down, key, 1) \
    SINGLE_EVENT(vname##_up, key, 0)   \
    SINGLE_EVENT(vname##_repeat, key, 2)

#define KEY_PAIR_EVENTS(vname, name) \
    KEY_EVENTS(left_##vname, LEFT##name), KEY_EVENTS(right_##vname, RIGHT##name)

// Kludge with double macro to be able to (macro)expand args before
// concatenation.
#define X_EVENT_VAR_NAME(key, event) key##_##event
#define EVENT_VAR_NAME(key, event) X_EVENT_VAR_NAME(key, event)
