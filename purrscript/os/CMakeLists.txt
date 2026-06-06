# purrscript os module — Python-like OS interface (filesystem, env, process,
# os.path). Built as its own static + shared library; linked only when a
# purrscript program `import os`s.
add_purrscript_library(os SOURCES os.cpp)
