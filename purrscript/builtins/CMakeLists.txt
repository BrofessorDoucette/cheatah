# cheatah builtins module — Python's always-available built-in functions
# (non-math subset). Its own static + shared library; the compiler links it into
# every program (built-ins need no import).
add_cheatah_library(builtins SOURCES builtins.cpp)
