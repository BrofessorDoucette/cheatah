# purrscript math module — mathematical utilities (Python `math` + math built-ins).
# Its own static + shared library; linked only when a program `import math`s.
#
# FUTURE: when this gains vector/matrix ops it will link cheatah's linalg core:
#   target_link_libraries(cheatah_purrscript_math_obj PUBLIC cheatah::linalg_static)
add_purrscript_library(math SOURCES math.cpp)
target_compile_options(cheatah_purrscript_math_obj PRIVATE -O3 -march=native -funroll-loops)
