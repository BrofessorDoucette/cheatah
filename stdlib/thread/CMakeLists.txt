# thread — run a cheatah `fn` on another OS thread: `thread.spawn(f, args...)` -> a join-on-destroy
# `Thread` guard. Shared state belongs to the `memory` module (Owner/Lease/Request); this module
# deliberately ships no second synchronization vocabulary.
add_cheatah_library(thread SOURCES thread.cpp)
find_package(Threads REQUIRED)
target_link_libraries(cheatah_thread_obj PUBLIC Threads::Threads)     # -pthread while compiling
target_link_libraries(cheatah_thread_shared PUBLIC Threads::Threads)  # and into the .so link
