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

#include <modules/ayni/src/ayniconnection.h>

#include <openspace/filesystem/filesystem.h>
#include <openspace/logging/logmanager.h>
#include <openspace/misc/exception.h>
#include <openspace/util/httprequest.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <vector>

namespace {
    using namespace openspace;

    constexpr std::string_view _loggerCat = "AyniConnection";

    constexpr std::string_view ConnectionSchema = "ayni-xr.openspace-connection.v1";
    constexpr std::string_view TokenRoute = "api/openspace/token";

    // A token is renewed this long before it expires, so a few failed attempts do not
    // interrupt the streams
    constexpr std::chrono::seconds RenewBefore = std::chrono::seconds(150);
    constexpr std::chrono::seconds MinimumWait = std::chrono::seconds(30);
    // The AYNI XR site asks for this wait when it limits the requests
    constexpr std::chrono::seconds RateLimitWait = std::chrono::seconds(60);
    // A licence without OpenSpace or streams may be renewed or extended later
    constexpr std::chrono::hours LicenceWait = std::chrono::hours(1);
    constexpr std::chrono::seconds RequestTimeout = std::chrono::seconds(20);

    [[noreturn]] void fail(std::string message) {
        throw RuntimeError(std::move(message), std::string(_loggerCat));
    }

    std::string readFile(const std::filesystem::path& path) {
        std::ifstream file = std::ifstream(path, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    std::string stringField(const nlohmann::json& json, const char* key) {
        const auto it = json.find(key);
        return (it != json.end() && it->is_string()) ? it->get<std::string>() : "";
    }

    bool isBase64UrlCharacter(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_';
    }

    // The connection's values and the token are sent in HTTP headers, so none of them
    // may be able to add a header
    bool isPlausibleToken(std::string_view token) {
        return token.starts_with("v1.") && token.size() <= 4096 &&
            std::all_of(
                token.begin(),
                token.end(),
                [](char c) { return isBase64UrlCharacter(c) || c == '.'; }
            );
    }

    bool isConnectionId(std::string_view id) {
        constexpr std::string_view Prefix = "osc_";
        return id.size() == Prefix.size() + 16 && id.starts_with(Prefix) &&
            std::all_of(
                id.begin() + Prefix.size(),
                id.end(),
                [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
            );
    }

    bool isSecret(std::string_view secret) {
        // 32 random bytes in base64url without padding
        return secret.size() == 43 &&
            std::all_of(secret.begin(), secret.end(), isBase64UrlCharacter);
    }

    bool isServerUrl(std::string_view url) {
        return url.starts_with("https://") && url.size() > 8 &&
            std::all_of(
                url.begin(),
                url.end(),
                [](char c) { return c > ' ' && c < 127; }
            );
    }
} // namespace

namespace openspace::ayni {

bool isOnStreamHost(std::string_view url) {
    return url.starts_with(StreamHost);
}

Connection parseConnection(std::string_view json) {
    const nlohmann::json file = nlohmann::json::parse(json, nullptr, false);
    if (!file.is_object() || stringField(file, "schema") != ConnectionSchema) {
        fail("The file is not an AYNI XR OpenSpace connection");
    }

    Connection connection = {
        .server = stringField(file, "server"),
        .id = stringField(file, "connection"),
        .secret = stringField(file, "secret"),
        .label = stringField(file, "label")
    };
    if (!isServerUrl(connection.server)) {
        fail("The connection's server is not an HTTPS address");
    }
    if (!connection.server.ends_with('/')) {
        connection.server += '/';
    }
    if (!isConnectionId(connection.id) || !isSecret(connection.secret)) {
        fail("The connection's identifier or secret is damaged");
    }
    return connection;
}

StreamToken parseStreamToken(std::string_view json) {
    const nlohmann::json answer = nlohmann::json::parse(json, nullptr, false);
    if (!answer.is_object()) {
        fail("The answer of AYNI XR is not readable");
    }

    StreamToken token = { .token = stringField(answer, "token") };
    if (!isPlausibleToken(token.token)) {
        fail("The answer of AYNI XR does not hold a token");
    }

    const std::string base = stringField(answer, "base");
    if (base != StreamHost) {
        fail(std::format(
            "AYNI XR names the stream host '{}', but OpenSpace sends tokens only to '{}'",
            base, StreamHost
        ));
    }

    // The lifetime rather than the expiry time, so that a computer whose clock is wrong
    // still renews in time
    const auto issuedAt = answer.find("issuedAt");
    const auto expiresAt = answer.find("expiresAt");
    if (issuedAt == answer.end() || !issuedAt->is_number() ||
        expiresAt == answer.end() || !expiresAt->is_number())
    {
        fail("The answer of AYNI XR does not say how long the token is valid");
    }
    const int64_t lifetimeMs = expiresAt->get<int64_t>() - issuedAt->get<int64_t>();
    token.lifetime = std::chrono::seconds(lifetimeMs / 1000);
    if (token.lifetime <= std::chrono::seconds(0) ||
        token.lifetime > std::chrono::hours(24))
    {
        fail("The answer of AYNI XR gives the token an impossible lifetime");
    }
    return token;
}

StreamAccess::StreamAccess() {
    const std::filesystem::path path = absPath("${USER}/ayni/connection.json");
    if (!std::filesystem::is_regular_file(path)) {
        return;
    }
    try {
        _connection = parseConnection(readFile(path));
    }
    catch (const RuntimeError& e) {
        fail(std::format("Could not use '{}': {}", path.string(), e.message));
    }
    LINFO(std::format("Using the AYNI XR connection '{}'", _connection->label));
}

StreamAccess::~StreamAccess() = default;

bool StreamAccess::hasConnection() const {
    return _connection.has_value();
}

std::optional<std::string> StreamAccess::token() {
    if (!_connection.has_value()) {
        return std::nullopt;
    }
    if (_token.empty()) {
        // Another layer may just have been refused; do not ask again before it is time
        const bool isWaiting = !_lastError.empty() &&
            std::chrono::steady_clock::now() < _nextRequest;
        if (!_isRefused && !isWaiting) {
            if (!_request) {
                startRequest();
            }
            _request->wait();
            finishRequest();
        }
        if (_token.empty()) {
            fail(_lastError);
        }
    }
    return _token;
}

void StreamAccess::update() {
    if (!_connection.has_value() || _isRefused) {
        return;
    }
    if (_request) {
        if (_request->hasSucceeded() || _request->hasFailed()) {
            finishRequest();
        }
    }
    else if (std::chrono::steady_clock::now() >= _nextRequest) {
        startRequest();
    }
}

int StreamAccess::generation() const {
    return _generation;
}

void StreamAccess::startRequest() {
    _request = std::make_unique<HttpMemoryDownload>(
        std::format("{}{}", _connection->server, TokenRoute),
        logging::LogLevel::Debug
    );
    _request->addRequestHeader(
        std::format("Authorization: Bearer {}.{}", _connection->id, _connection->secret)
    );
    _request->addRequestHeader("Content-Type: application/json");
    _request->setPostBody("{}");
    _request->start(RequestTimeout);
}

void StreamAccess::finishRequest() {
    const bool isSuccess = _request->hasSucceeded();
    const std::vector<char>& data = _request->downloadedData();
    const std::string body = std::string(data.begin(), data.end());
    _request = nullptr;
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    if (isSuccess) {
        try {
            StreamToken token = parseStreamToken(body);
            _token = std::move(token.token);
            _generation++;
            _nextRequest = now + std::max(token.lifetime - RenewBefore, MinimumWait);
            LDEBUG("Received a new AYNI stream token");
        }
        catch (const RuntimeError& e) {
            _lastError = e.message;
            _nextRequest = now + MinimumWait;
            LWARNING(_lastError);
        }
        return;
    }

    const nlohmann::json answer = nlohmann::json::parse(body, nullptr, false);
    const std::string error = answer.is_object() ? stringField(answer, "error") : "";
    const std::string& label = _connection->label;
    if (error == "unknown_connection") {
        _isRefused = true;
        _lastError = std::format(
            "AYNI XR no longer knows the connection '{}'. It was revoked or its licence "
            "was given back; put a new connection file from the AYNI XR plan page in "
            "${{USER}}/ayni/connection.json",
            label
        );
        LERROR(_lastError);
    }
    else if (error == "no_openspace" || error == "no_streams") {
        _nextRequest = now + LicenceWait;
        _lastError = std::format(
            "The AYNI XR licence of the connection '{}' does not include {}; OpenSpace "
            "asks again in an hour",
            label, error == "no_openspace" ? "OpenSpace" : "any streams"
        );
        LERROR(_lastError);
    }
    else if (error == "rate_limited") {
        _lastError = "AYNI XR asked to wait before the next stream token";
        _nextRequest = now + RateLimitWait;
        LWARNING(_lastError);
    }
    else {
        _lastError = error.empty() ?
            "Could not reach AYNI XR for a stream token" :
            std::format("AYNI XR did not give a stream token ({})", error);
        _nextRequest = now + MinimumWait;
        LWARNING(_lastError);
    }
}

} // namespace openspace::ayni
