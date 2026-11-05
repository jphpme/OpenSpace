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

#include <modules/ayni/src/aynilicence.h>

#include <openspace/filesystem/filesystem.h>
#include <openspace/logging/logmanager.h>
#include <openspace/misc/exception.h>
#include <openspace/openspace.h>
#include <openspace/util/httprequest.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <sstream>

namespace {
    using namespace openspace;

    constexpr std::string_view _loggerCat = "AyniLicence";

    constexpr std::string_view LicenceServer = "https://licence.pachamama-studios.stream";

    // @TODO (2026-11-05, jphpme) Use a product of its own (`ayni-openspace`) once the
    // licence server knows it; until then OpenSpace activates desktop (AYNI 2D) keys
    constexpr std::string_view Product = "ayni-2d";

    // The desktop apps renew a token this old; the stream host accepts it for 24 hours
    constexpr std::chrono::hours RefreshAge = std::chrono::hours(20);

    constexpr std::string_view KeyAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    constexpr size_t KeyLength = 20;

    struct StoredLicence {
        std::string installationId;
        std::string token;
        std::string keyHint;
        int64_t obtainedAt = 0;
    };

    int64_t now() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    [[noreturn]] void fail(std::string message) {
        throw RuntimeError(std::move(message), std::string(_loggerCat));
    }

    std::string readFile(const std::filesystem::path& path) {
        std::ifstream file = std::ifstream(path, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    std::string newInstallationId() {
        std::random_device device;
        std::mt19937_64 generator = std::mt19937_64(
            (static_cast<uint64_t>(device()) << 32) ^ device()
        );
        const uint64_t high = generator();
        const uint64_t low = generator();
        return std::format("{:016x}{:016x}", high, low);
    }

    StoredLicence loadState(const std::filesystem::path& path) {
        StoredLicence state;
        if (std::filesystem::is_regular_file(path)) {
            const nlohmann::json json =
                nlohmann::json::parse(readFile(path), nullptr, false);
            if (json.is_object()) {
                state.installationId = json.value("installationId", "");
                state.token = json.value("token", "");
                state.keyHint = json.value("keyHint", "");
                state.obtainedAt = json.value("obtainedAt", int64_t(0));
            }
        }
        if (state.installationId.empty()) {
            state.installationId = newInstallationId();
        }
        return state;
    }

    void saveState(const std::filesystem::path& path, const StoredLicence& state) {
        const nlohmann::json json = {
            { "installationId", state.installationId },
            { "token", state.token },
            { "keyHint", state.keyHint },
            { "obtainedAt", state.obtainedAt }
        };
        std::ofstream file = std::ofstream(path, std::ios::binary);
        file << json.dump(2);
    }

    struct Response {
        bool success = false;
        nlohmann::json body;
    };

    Response post(std::string_view route, const nlohmann::json& body) {
        HttpMemoryDownload request = HttpMemoryDownload(
            std::format("{}{}", LicenceServer, route),
            logging::LogLevel::Debug
        );
        request.addRequestHeader("Content-Type: application/json");
        request.setPostBody(body.dump());
        request.start(std::chrono::seconds(20));
        Response response;
        response.success = request.wait();
        const std::vector<char>& data = request.downloadedData();
        response.body = nlohmann::json::parse(data.begin(), data.end(), nullptr, false);
        if (!response.body.is_object()) {
            response.body = nlohmann::json::object();
        }
        return response;
    }

    // The token is sent in HTTP headers, so it must not be able to add a header
    bool isPlausibleToken(std::string_view token) {
        if (!token.starts_with("v1.") || token.size() > 4096) {
            return false;
        }
        for (const char c : token) {
            const bool isAllowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
            if (!isAllowed) {
                return false;
            }
        }
        return true;
    }

    std::string tokenFrom(const Response& response) {
        const std::string token = response.body.value("token", "");
        if (!isPlausibleToken(token)) {
            fail("The licence server's answer did not hold a token");
        }
        return token;
    }
} // namespace

namespace openspace::ayni {

bool isOnStreamHost(std::string_view url) {
    return url.starts_with(StreamHost);
}

std::optional<std::string> normalizeLicenceKey(std::string_view input) {
    std::string body;
    for (const char c : input) {
        const char upper = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
        if (upper == 'O') {
            body += '0';
        }
        else if (upper == 'I' || upper == 'L') {
            body += '1';
        }
        else if ((upper >= 'A' && upper <= 'Z') || (upper >= '0' && upper <= '9')) {
            body += upper;
        }
    }
    // `AYNI` reads as `AYN1` once `I` became `1`
    if (body.size() == KeyLength + 4 && body.starts_with("AYN1")) {
        body = body.substr(4);
    }
    if (body.size() != KeyLength) {
        return std::nullopt;
    }

    size_t sum = 0;
    for (size_t i = 0; i < KeyLength; i++) {
        const size_t index = KeyAlphabet.find(body[i]);
        if (index == std::string_view::npos) {
            return std::nullopt;
        }
        if (i < KeyLength - 1) {
            sum += index * (i + 1);
        }
    }
    if (KeyAlphabet[sum % 31] != body[KeyLength - 1]) {
        return std::nullopt;
    }
    return std::format(
        "AYNI-{}-{}-{}-{}",
        body.substr(0, 5), body.substr(5, 5), body.substr(10, 5), body.substr(15, 5)
    );
}

std::optional<std::string> streamToken() {
    const std::filesystem::path keyPath = absPath("${USER}/ayni/licence.key");
    if (!std::filesystem::is_regular_file(keyPath)) {
        return std::nullopt;
    }
    const std::optional<std::string> key = normalizeLicenceKey(readFile(keyPath));
    if (!key.has_value()) {
        fail(std::format("'{}' does not hold a valid licence key", keyPath.string()));
    }
    const std::string keyHint = key->substr(key->size() - 4);

    const std::filesystem::path statePath = absPath("${USER}/ayni/licence.json");
    StoredLicence state = loadState(statePath);
    if (state.keyHint != keyHint) {
        // A token belongs to the key it was issued for
        state.token.clear();
    }

    const int64_t age = now() - state.obtainedAt;
    const int64_t refreshAge =
        std::chrono::duration_cast<std::chrono::seconds>(RefreshAge).count();
    if (!state.token.empty() && age < refreshAge) {
        return state.token;
    }

    const std::string version = std::string(OPENSPACE_VERSION);
    if (!state.token.empty()) {
        const Response response = post(
            "/license/refresh",
            { { "token", state.token }, { "app_version", version } }
        );
        if (response.success) {
            state.token = tokenFrom(response);
            state.obtainedAt = now();
            saveState(statePath, state);
            LINFO("Refreshed the AYNI licence token");
            return state.token;
        }
        if (response.body.value("error", "") != "invalid_token") {
            // Not reachable, or refused for now: the stream host decides whether the
            // token still holds
            LWARNING(std::format(
                "Could not refresh the AYNI licence token: {}",
                response.body.value("message", "no answer")
            ));
            return state.token;
        }
        // The server no longer knows this installation; activate the key again
    }

    const Response response = post(
        "/license/activate",
        {
            { "key", *key },
            { "product", Product },
            { "installation_id", state.installationId },
            { "label", "OpenSpace" },
            { "app_version", version }
        }
    );
    if (!response.success) {
        fail(std::format(
            "The AYNI licence server did not accept the key ending in {}: {}",
            keyHint, response.body.value("message", "no answer")
        ));
    }
    state.token = tokenFrom(response);
    state.keyHint = keyHint;
    state.obtainedAt = now();
    saveState(statePath, state);
    LINFO(std::format("Activated the AYNI licence key ending in {}", keyHint));
    return state.token;
}

} // namespace openspace::ayni
