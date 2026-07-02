# cheatah parsers module — C++-authored (templates/concepts/variant are beyond the current .purr
# subset; `requests` is the first pure-cheatah stdlib module). `import parsers` maps to
# parsers.hpp + libcheatah_parsers.
add_cheatah_library(parsers SOURCES json/json.cpp)
