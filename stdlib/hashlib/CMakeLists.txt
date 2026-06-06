# cheatah hashlib module — its own static + shared library; linked only on `import hashlib`.
add_cheatah_library(hashlib SOURCES hashlib.cpp)
