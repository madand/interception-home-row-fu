
// Modify these to change key<->modifier correspondence to your
// taste. Available key constants are listed in <input-event-codes.h>,
// usually located at: /usr/include/linux/input-event-codes.h
// Hint: META is a Windows-key on most keyboards.
//
// The third parameter controls whether to simulate the modifier press
// immediately after the key press (e.g. to be able to use Ctrl + Mouse Scroll
// etc.). Should be generally set to false for Alt, because GUI programs tend to
// activate main menu when pressing Alt.

DEFINE_MAPPING(KEY_A, KEY_LEFTALT, false)
DEFINE_MAPPING(KEY_SEMICOLON, KEY_RIGHTALT, false)

DEFINE_MAPPING(KEY_S, KEY_LEFTSHIFT, true)
DEFINE_MAPPING(KEY_L, KEY_RIGHTSHIFT, true)

DEFINE_MAPPING(KEY_D, KEY_LEFTMETA, true)
DEFINE_MAPPING(KEY_K, KEY_RIGHTMETA, true)

DEFINE_MAPPING(KEY_F, KEY_LEFTCTRL, true)
DEFINE_MAPPING(KEY_J, KEY_RIGHTCTRL, true)
