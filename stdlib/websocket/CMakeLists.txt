# cheatah websocket module — a from-scratch RFC 6455 WebSocket client built on the
# cheatah tls 1.3 client + socket (+ os.urandom for the masking/handshake keys). No
# external libraries. Links the object libs of its dependencies for their headers.
add_cheatah_library(websocket SOURCES websocket.cpp)
target_link_libraries(cheatah_websocket_obj PRIVATE
    cheatah_tls_obj cheatah_socket_obj cheatah_os_obj)
