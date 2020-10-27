project('interception-home-row-fu', 'c',
  version : '0.1',
  default_options : ['warning_level=3', 'c_std=c99'])

libevdev_dep = dependency('libevdev')

libtomlc99_dep = dependency('libtomlc99', required : false)
if not libtomlc99_dep.found()
  libtomlc99_proj = subproject('libtomlc99')
  libtomlc99_dep = libtomlc99_proj.get_variable('libtomlc99_dep')
endif

executable('home-row-fu',
           ['home-row-fu.c'],
           dependencies: [libevdev_dep, libtomlc99_dep],
           install : true)
