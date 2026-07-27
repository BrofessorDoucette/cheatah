# cheatah websocket module — a from-scratch RFC 6455 WebSocket client built on the
# cheatah tls 1.3 client + socket (+ os.urandom for the masking/handshake keys). No
# external libraries.
add_cheatah_library(websocket
    SOURCES websocket.cpp
    DEPENDS tls socket os)
