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

#include <modules/ayni/src/aynicatalogue.h>

#include <modules/ayni/src/ayniconnection.h>
#include <openspace/misc/exception.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <format>

namespace {
    using namespace openspace;

    constexpr std::string_view _loggerCat = "AyniCatalogue";

    constexpr std::string_view LayerPrefix = "AyniStream_";

    std::string stringField(const nlohmann::json& json, const char* key) {
        const auto it = json.find(key);
        return (it != json.end() && it->is_string()) ? it->get<std::string>() : "";
    }

    bool isTrue(const nlohmann::json& json, const char* key) {
        const auto it = json.find(key);
        return it != json.end() && it->is_boolean() && it->get<bool>();
    }

    std::string capitalized(std::string text) {
        if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') {
            text[0] = static_cast<char>(text[0] - 'a' + 'A');
        }
        return text;
    }

    // Stream names become parts of layer and action identifiers
    bool isStreamName(std::string_view name) {
        return !name.empty() && name.size() <= 128 &&
            std::all_of(
                name.begin(),
                name.end(),
                [](char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
                }
            );
    }

    // A path on the stream host, so the licence token is never sent elsewhere
    bool isHostPath(std::string_view path) {
        return !path.empty() && !path.starts_with('/') &&
            path.find("..") == std::string_view::npos &&
            path.find(':') == std::string_view::npos &&
            std::all_of(
                path.begin(),
                path.end(),
                [](char c) { return c > ' ' && c < 127; }
            );
    }

    // The scene graph node of a catalogue's `celestialBody`; streams name none for Earth
    std::string globeOf(std::string body) {
        if (body.empty()) {
            return "Earth";
        }
        const bool isWord = std::all_of(
            body.begin(),
            body.end(),
            [](char c) { return c >= 'a' && c <= 'z'; }
        );
        return isWord ? capitalized(std::move(body)) : "";
    }
} // namespace

namespace openspace::ayni {

std::vector<CatalogueStream> parseCatalogue(std::string_view json) {
    const nlohmann::json catalogue = nlohmann::json::parse(json, nullptr, false);
    const nlohmann::json* datasets = nullptr;
    if (catalogue.is_object()) {
        const auto it = catalogue.find("datasets");
        if (it != catalogue.end() && it->is_array()) {
            datasets = &*it;
        }
    }
    if (!datasets) {
        throw RuntimeError(
            "The stream host's catalogue is not readable",
            std::string(_loggerCat)
        );
    }

    std::vector<CatalogueStream> streams;
    for (const nlohmann::json& entry : *datasets) {
        if (!entry.is_object() || stringField(entry, "type") != "stream" ||
            !isTrue(entry, "valueEncoded"))
        {
            continue;
        }

        CatalogueStream stream = {
            .identifier = stringField(entry, "name"),
            .name = stringField(entry, "display_name"),
            .description = stringField(entry, "description"),
            .category = "Other",
            .globe = globeOf(stringField(entry, "celestialBody"))
        };
        const std::string path = stringField(entry, "releaseDescriptorUrl");
        if (!isStreamName(stream.identifier) || stream.name.empty() ||
            stream.globe.empty() || !isHostPath(path))
        {
            continue;
        }
        stream.url = std::format("{}{}", StreamHost, path);
        if (stringField(entry, "coverage") != "global") {
            stream.region = stringField(entry, "regionName");
            if (stream.region.empty()) {
                stream.region = capitalized(stringField(entry, "region"));
            }
            if (stream.region.empty()) {
                stream.region = "Other regions";
            }
        }

        const auto categories = entry.find("categories");
        if (categories != entry.end() && categories->is_array() &&
            !categories->empty() && categories->front().is_string())
        {
            stream.category = capitalized(categories->front().get<std::string>());
        }
        streams.push_back(std::move(stream));
    }
    return streams;
}

std::string layerIdentifier(const CatalogueStream& stream) {
    return std::format("{}{}", LayerPrefix, stream.identifier);
}

bool isStreamLayer(std::string_view identifier) {
    return identifier.starts_with(LayerPrefix);
}

} // namespace openspace::ayni
