project('interception-home-row-fu', 'c',
  version : '0.1',
  default_options : ['warning_level=3'])

executable('home-row-fu',
           ['home-row-fu.c', 'toml.c'],
           dependencies: dependency('libevdev'),
           install : true)
