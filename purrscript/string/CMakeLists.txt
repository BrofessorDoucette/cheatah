# purrscript string module — common text operations + Python `string` constants.
# Its own static + shared library; linked only when a program `import string`s.
add_purrscript_library(string SOURCES string.cpp)
