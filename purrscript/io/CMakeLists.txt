# cheatah io module — Python-like input/output (print, input, str/repr/format,
# open/File). Built as its own static + shared library; linked only when a
# cheatah program `import io`s.
add_cheatah_library(io SOURCES io.cpp)
