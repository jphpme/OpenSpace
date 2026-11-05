/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2026                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#ifdef OPENSPACE_MODULE_AYNI_ENABLED

#include <catch2/catch_test_macros.hpp>

#include <modules/ayni/src/ayniconnection.h>
#include <openspace/misc/exception.h>
#include <format>
#include <string>

using namespace openspace;
using namespace openspace::ayni;

namespace {
    constexpr std::string_view Secret = "AbCdEfGhIjKlMnOpQrStUvWxYz0123456789-_AbCdE";

    std::string connectionFile(std::string_view server, std::string_view id,
                               std::string_view secret)
    {
        return std::format(
            R"({{ "schema": "ayni-xr.openspace-connection.v1", "server": "{}", )"
            R"("connection": "{}", "secret": "{}", "label": "Dome, show PC" }})",
            server, id, secret
        );
    }

    std::string tokenAnswer(std::string_view token, std::string_view base) {
        return std::format(
            R"({{ "token": "{}", "issuedAt": 1762340000000, )"
            R"("expiresAt": 1762340600000, "base": "{}" }})",
            token, base
        );
    }
} // namespace

TEST_CASE("AyniConnection: Parse connection", "[ayniconnection]") {
    const Connection connection = parseConnection(
        connectionFile("https://vr.ayni.eu.com/", "osc_0123456789abcdef", Secret)
    );
    CHECK(connection.server == "https://vr.ayni.eu.com/");
    CHECK(connection.id == "osc_0123456789abcdef");
    CHECK(connection.secret == Secret);
    CHECK(connection.label == "Dome, show PC");

    // A server without the final slash
    CHECK(
        parseConnection(
            connectionFile("https://vr.ayni.eu.com", "osc_0123456789abcdef", Secret)
        ).server == "https://vr.ayni.eu.com/"
    );
}

TEST_CASE("AyniConnection: Reject connection", "[ayniconnection]") {
    CHECK_THROWS_AS(parseConnection(""), RuntimeError);
    CHECK_THROWS_AS(parseConnection(R"({ "schema": "something.else" })"), RuntimeError);
    // Not HTTPS
    CHECK_THROWS_AS(
        parseConnection(
            connectionFile("http://vr.ayni.eu.com/", "osc_0123456789abcdef", Secret)
        ),
        RuntimeError
    );
    // A damaged identifier
    CHECK_THROWS_AS(
        parseConnection(
            connectionFile("https://vr.ayni.eu.com/", "osc_0123456789ABCDEF", Secret)
        ),
        RuntimeError
    );
    // A secret that is too short, or that would add a header
    CHECK_THROWS_AS(
        parseConnection(
            connectionFile("https://vr.ayni.eu.com/", "osc_0123456789abcdef", "abc")
        ),
        RuntimeError
    );
    CHECK_THROWS_AS(
        parseConnection(
            connectionFile(
                "https://vr.ayni.eu.com/",
                "osc_0123456789abcdef",
                "AbCdEfGhIjKlMnOpQrStUvWxYz0123456789-_A\r\nX:"
            )
        ),
        RuntimeError
    );
}

TEST_CASE("AyniConnection: Parse token answer", "[ayniconnection]") {
    const StreamToken token = parseStreamToken(
        tokenAnswer("v1.xr-stream-1.eyJ2IjoxfQ.c2ln", "https://pachamama-studios.stream/")
    );
    CHECK(token.token == "v1.xr-stream-1.eyJ2IjoxfQ.c2ln");
    CHECK(token.lifetime == std::chrono::seconds(600));

    // Another stream host, a token that would add a header, or no token
    CHECK_THROWS_AS(
        parseStreamToken(tokenAnswer("v1.a.b.c", "https://example.com/")),
        RuntimeError
    );
    CHECK_THROWS_AS(
        parseStreamToken(
            tokenAnswer("v1.a.b.c\r\nX: y", "https://pachamama-studios.stream/")
        ),
        RuntimeError
    );
    CHECK_THROWS_AS(
        parseStreamToken(R"({ "error": "unknown_connection" })"),
        RuntimeError
    );
}

TEST_CASE("AyniConnection: Stream host", "[ayniconnection]") {
    CHECK(isOnStreamHost("https://pachamama-studios.stream/global/x/latest.json"));
    CHECK_FALSE(isOnStreamHost("http://pachamama-studios.stream/global/x/latest.json"));
    CHECK_FALSE(isOnStreamHost("https://pachamama-studios.stream.example.com/x"));
    CHECK_FALSE(isOnStreamHost("https://example.com/pachamama-studios.stream/x"));
}

#endif // OPENSPACE_MODULE_AYNI_ENABLED
