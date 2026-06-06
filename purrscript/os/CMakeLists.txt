# cheatah os module — Python-like OS interface (filesystem, env, process,
# os.path). Built as its own static + shared library; linked only when a
# cheatah program `import os`s.
add_cheatah_library(os SOURCES os.cpp)
