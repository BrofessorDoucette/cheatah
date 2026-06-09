# cheatah html module — HTML escaping + a tolerant tokenizing parser.
# Its own static + shared library; linked only when a program `import html`s.
add_cheatah_library(html SOURCES html.cpp)
