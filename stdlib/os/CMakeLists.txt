# cheatah os module — Python-like OS interface (filesystem, env, process,
# os.path). Built as its own static + shared library; linked only when a
# cheatah program `import os`s.
add_cheatah_library(os SOURCES os.cpp)

# os.urandom uses BCryptGenRandom on Windows (getentropy elsewhere, no extra lib).
if(WIN32)
    target_link_libraries(cheatah_os_shared PUBLIC bcrypt)
    target_link_libraries(cheatah_os_static INTERFACE bcrypt)
endif()
