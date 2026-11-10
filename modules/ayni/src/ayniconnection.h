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

#ifndef __OPENSPACE_MODULE_AYNI___AYNICONNECTION___H__
#define __OPENSPACE_MODULE_AYNI___AYNICONNECTION___H__

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace openspace { class HttpMemoryDownload; }

namespace openspace::ayni {

/// The AYNI stream host; the only host that a stream token is ever sent to
constexpr std::string_view StreamHost = "https://pachamama-studios.stream/";

/// Returns whether \p url is a URL on the AYNI stream host.
bool isOnStreamHost(std::string_view url);

/// Returns the HTTP header that presents \p token to the AYNI stream host.
std::string authorizationHeader(std::string_view token);

/**
 * A connection, the file that an AYNI XR plan hands out per installation: an OpenSpace
 * connection for a dome (`ayni-xr.openspace-connection.v1`) or a node connection of the
 * `ayni-access/1` profile (`ayni-xr.node-connection.v1`).
 */
struct Connection {
    /// The AYNI XR site that issued the connection, ending in `/`
    std::string server;
    /// The connection's identifier: `osc_` (OpenSpace) or `anc_` (node) followed by 16
    /// hexadecimal digits
    std::string id;
    std::string secret;
    /// The name the plan's owner gave this installation
    std::string label;
    /// The address on `server` that exchanges the connection for stream tokens
    std::string tokenEndpoint;
};

/**
 * Parses the contents of a connection file.
 *
 * \param json The contents of the file
 * \return The connection that the file describes
 *
 * \throw RuntimeError If \p json is not an OpenSpace or node connection file, or a node
 *        connection names a token endpoint that is not on its own server
 */
Connection parseConnection(std::string_view json);

/// A stream token as the AYNI XR site issues it
struct StreamToken {
    std::string token;
    /// How long the token is valid, counted from when it was issued
    std::chrono::seconds lifetime = std::chrono::seconds(0);
    /// When to ask for the next token, counted from when this one was issued
    std::chrono::seconds renewAfter = std::chrono::seconds(0);
    /// The use class of the connection's licence, such as `education` or `venue`, or
    /// empty if the answer names none
    std::string use;
    /// The name of the connection's plan, or empty if the answer names none
    std::string plan;
};

/**
 * Parses the answer of the AYNI XR site's token route. Without a `renewAfter` in the
 * answer, the token is renewed 150 seconds before it expires.
 *
 * \param json The body of the answer
 * \return The token that the answer holds
 *
 * \throw RuntimeError If the answer holds no usable token, or names another stream host
 *        than #StreamHost
 */
StreamToken parseStreamToken(std::string_view json);

/**
 * Holds the stream token of this installation's connection, which is read from
 * `${USER}/ayni/connection.json`. Tokens live for ten minutes, so #update renews the
 * token in the background while streams play. A connection that the AYNI XR site no
 * longer knows is not asked again; one whose licence does not include OpenSpace, node
 * access or any streams is asked again once an hour, as a renewed licence makes the same
 * file work.
 */
class StreamAccess {
public:
    /// Reads the connection file, if there is one
    StreamAccess();
    ~StreamAccess();

    /// Returns whether this installation has a connection file
    bool hasConnection() const;

    /**
     * Returns the current stream token, or no value if there is no connection file. If
     * there is no current token yet, this call blocks while it asks the AYNI XR site.
     *
     * \throw RuntimeError If the AYNI XR site does not give a token
     */
    std::optional<std::string> token();

    /// Renews the token when it is about to expire, without blocking. Call every frame.
    void update();

    /// Returns a number that changes whenever the token does
    int generation() const;

    /// Returns when the current token expires
    std::chrono::steady_clock::time_point expiresAt() const;

private:
    void startRequest();

    /// Takes the token from the finished request, or plans when to ask again
    void finishRequest();

    std::optional<Connection> _connection;
    std::string _token;
    /// The plan that the last token was issued for, to tell the operator when it changes
    std::string _plan;
    /// Why the last request did not give a token
    std::string _lastError;
    int _generation = 0;
    bool _isRefused = false;
    std::chrono::steady_clock::time_point _expiresAt;
    std::chrono::steady_clock::time_point _nextRequest;
    std::unique_ptr<HttpMemoryDownload> _request;
};

} // namespace openspace::ayni

#endif // __OPENSPACE_MODULE_AYNI___AYNICONNECTION___H__
