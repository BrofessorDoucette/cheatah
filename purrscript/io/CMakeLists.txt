# purrscript io module — Python-like input/output (print, input, str/repr/format,
# open/File). Built as its own static + shared library; linked only when a
# purrscript program `import io`s.
add_purrscript_library(io SOURCES io.cpp)
