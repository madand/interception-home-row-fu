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
make
make install
# Invoke the following command only if you install for the first time, as it
# will overwrite any existing config file!
make install-config-file
```

Caveats
-------

The following caveats only pertain to the keys the plugin is configured to
handle; all events pertaining to the other keys are just passed through.

  * Visual lag when entering one of the handled keys. This is by design: the
    keys are being sent either after the next key press (during burst typing) or
    after the key release.

  * Need to slow down for using modifiers in order to wait out the burst typing
    time window (200 msec by default).

  * Key Repeat events are discarded, because on longish press (more that 700
    msec by default) keys are being "locked" to be a modifier and would insert
    nothing when released.

TODO
----

  * Fix interaction of e.g. the real Left Shift holding and pressing A (which
    emulates) Left Shift.

License
-------

This project is licensed under the terms of the MIT license.

Related Projects
----------------

  * [Interception Tools](https://gitlab.com/interception/linux/tools)
  * [Dual Function Keys](https://gitlab.com/interception/linux/plugins/dual-function-keys/)
