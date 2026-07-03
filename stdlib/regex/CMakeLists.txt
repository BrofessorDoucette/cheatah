# cheatah regex module — a from-scratch linear-time (lazy-DFA) engine; its own static + shared
# library, linked only on `import regex`.
add_cheatah_library(regex SOURCES regex.cpp)
