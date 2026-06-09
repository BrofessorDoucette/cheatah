# cheatah sys module — system-specific parameters (sys.argv). Built as its own
# static + shared library; linked when a cheatah program `import sys`s. It exports
# the `cheatah_set_argv` hook the runtime calls to forward the program's
# command-line arguments into sys.argv before it runs.
add_cheatah_library(sys SOURCES sys.cpp)
