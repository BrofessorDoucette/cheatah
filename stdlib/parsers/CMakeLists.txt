# parsers — the first pure-cheatah stdlib module: safe input parsers authored in `.purr`
# and emitted by purrc as a TRANSPARENT (source-in-header) importable module. Built BY purrc,
# so the root CMakeLists adds it after the compiler/ subdirectory (where purrc is defined).
# The cheatah_add_module helper is included once at the root.
cheatah_add_module(parsers SOURCES parsers.purr TRANSPARENT)
