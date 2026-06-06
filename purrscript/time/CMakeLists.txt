# purrscript time module — high-accuracy timing on C++ <chrono> clocks.
# Its own static + shared library; linked only when a program `import time`s.
add_purrscript_library(time SOURCES time.cpp)
