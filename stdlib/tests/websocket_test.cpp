// websocket_test — offline unit checks for the websocket client. The framing
// and live round-trip are covered by the system tests (a real WSS server);
// here we cover the input validation that needs no network.

#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "websocket.hpp"

namespace ws = cheatah::websocket;

TEST(CheatahWebSocket, ConnectUrlRejectsNonWss) {
    // Only wss:// is supported; a non-wss scheme fails fast, before any socket work. open_url()
    // is the cheatah-facing guard factory (it delegates to the C++-only connect_url()).
    EXPECT_THROW(ws::open_url("ws://example.com/"), std::runtime_error);
    EXPECT_THROW(ws::open_url("https://example.com/"), std::runtime_error);
    EXPECT_THROW(ws::open_url("example.com"), std::runtime_error);
}

// A default-constructed Client owns nothing: closed, id() == 0, and close() reports -1 without
// touching a session. open_url() on a non-wss scheme throws before a session is ever created,
// so the guard is never left holding a bogus handle. (Functional send/recv/close over a live
// session are covered by WebSocketSys.EchoRoundTrip against a real wss peer.)
TEST(CheatahWebSocket, ClientDefaultIsClosed) {
    ws::Client c;
    EXPECT_FALSE(c.is_open());
    EXPECT_EQ(c.id(), 0);
    EXPECT_EQ(c.close(), -1);  // nothing to close
    EXPECT_THROW(ws::open_url("ws://example.com/"), std::runtime_error);
}
