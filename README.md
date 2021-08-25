Home Row Fu
===========

Make the home row keys (`A S D F` and `J K L ;`) act as modifiers (`Shift`,
`Ctrl`, `Meta`, `Alt`) in addition to their default function.

Installation
------------

This is a [Interception Tools](https://gitlab.com/interception/linux/tools)
plugin so you need to have them installed. The plugin itself has no external
dependencies and can be compiled as simple as:

``` shell
gcc -o home-row-fu home-row-fu.c
```

Caveats
-------

All of the following caveats pertain only to the keys this plugin handles, all
other keys are unaffected (their events are passed through).

  * Visual lag when entering one of the handled keys. This is by design: the
    keys are being sent either after the next key press (during burst typing) or
    after the key release.

  * Need to slow down for using modifiers in order to wait out the burst typing
    time window (200 msec by default).

  * Key Repeat events are discarded, because on longish press (more that 700
    msec by default) keys are being "locked" as modifier and insert nothing when
    released.

License
-------

This project is licensed under the terms of the MIT license.

Related Projects
----------------

  * [Interception Tools](https://gitlab.com/interception/linux/tools)
  * [Dual Function Keys](https://gitlab.com/interception/linux/plugins/dual-function-keys/)
