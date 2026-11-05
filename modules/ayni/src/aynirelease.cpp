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

#include <modules/ayni/src/aynirelease.h>

#include <openspace/misc/exception.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace {
    using namespace openspace;
    using namespace openspace::ayni;

    constexpr std::string_view PointerSchema = "realtime-new.release-pointer.v1";
    constexpr std::string_view ReleaseSchema = "realtime-new.immutable-dash-release.v1";

    [[noreturn]] void fail(std::string_view message) {
        throw RuntimeError(std::string(message), "AyniRelease");
    }

    const nlohmann::json& member(const nlohmann::json& object, std::string_view key) {
        const auto it = object.find(key);
        if (it == object.end()) {
            fail(std::format("Missing '{}'", key));
        }
        return *it;
    }

    std::string stringMember(const nlohmann::json& object, std::string_view key) {
        const nlohmann::json& value = member(object, key);
        if (!value.is_string()) {
            fail(std::format("'{}' is not a string", key));
        }
        return value.get<std::string>();
    }

    double numberMember(const nlohmann::json& object, std::string_view key) {
        const nlohmann::json& value = member(object, key);
        if (!value.is_number()) {
            fail(std::format("'{}' is not a number", key));
        }
        return value.get<double>();
    }

    int codeMember(const nlohmann::json& object, std::string_view key) {
        const double value = numberMember(object, key);
        if (value < 0.0 || value > 255.0 || std::floor(value) != value) {
            fail(std::format("'{}' is not a code between 0 and 255", key));
        }
        return static_cast<int>(value);
    }

    // Removes `.` and `..` segments from an absolute URL path.
    std::string normalizePath(std::string_view path) {
        std::vector<std::string_view> segments;
        size_t begin = 1;
        while (begin <= path.size()) {
            size_t end = path.find('/', begin);
            if (end == std::string_view::npos) {
                end = path.size();
            }
            const std::string_view segment = path.substr(begin, end - begin);
            if (segment == "..") {
                if (!segments.empty()) {
                    segments.pop_back();
                }
            }
            else if (segment != ".") {
                segments.push_back(segment);
            }
            begin = end + 1;
        }
        // A trailing `.` or `..` still names a directory
        const bool isDirectory = path.ends_with("/.") || path.ends_with("/..");

        std::string result;
        for (const std::string_view segment : segments) {
            result += '/';
            result += segment;
        }
        if (isDirectory || result.empty()) {
            result += '/';
        }
        return result;
    }

    ValueEncoding parseEncoding(const nlohmann::json& json, glm::ivec2 frameSize) {
        ValueEncoding enc;

        const std::string kind = stringMember(json, "kind");
        if (kind == "luma8-linear") {
            enc.kind = ValueEncoding::Kind::Linear;
        }
        else if (kind == "luma8-log") {
            enc.kind = ValueEncoding::Kind::Log;
        }
        else if (kind == "luma8-classified") {
            enc.kind = ValueEncoding::Kind::Classified;
        }
        else {
            fail(std::format("Unknown value encoding '{}'", kind));
        }

        enc.nodataThresholdCode = codeMember(json, "nodataThresholdCode");
        enc.dataMinCode = codeMember(json, "dataMinCode");
        enc.dataMaxCode = codeMember(json, "dataMaxCode");
        if (enc.dataMaxCode <= enc.dataMinCode) {
            fail("'dataMaxCode' must be larger than 'dataMinCode'");
        }
        enc.vmin = numberMember(json, "vmin");
        enc.vmax = numberMember(json, "vmax");
        if (enc.kind == ValueEncoding::Kind::Log && !(enc.vmin > 0.0 && enc.vmax > 0.0)) {
            fail("A logarithmic encoding needs positive 'vmin' and 'vmax'");
        }
        if (json.contains("units") && json["units"].is_string()) {
            enc.units = json["units"].get<std::string>();
        }

        if (json.contains("dataRegion")) {
            const nlohmann::json& region = json["dataRegion"];
            enc.dataRegionOffset = glm::ivec2(
                static_cast<int>(numberMember(region, "x")),
                static_cast<int>(numberMember(region, "y"))
            );
            enc.dataRegionSize = glm::ivec2(
                static_cast<int>(numberMember(region, "width")),
                static_cast<int>(numberMember(region, "height"))
            );
        }
        else {
            enc.dataRegionSize = frameSize;
        }
        const glm::ivec2 regionEnd = enc.dataRegionOffset + enc.dataRegionSize;
        if (enc.dataRegionOffset.x < 0 || enc.dataRegionOffset.y < 0 ||
            enc.dataRegionSize.x <= 0 || enc.dataRegionSize.y <= 0 ||
            regionEnd.x > frameSize.x || regionEnd.y > frameSize.y)
        {
            fail("'dataRegion' does not fit in the frame");
        }

        if (json.contains("geo") && json["geo"].is_object()) {
            const nlohmann::json& geo = json["geo"];
            enc.latTop = numberMember(geo, "latTop");
            enc.latBottom = numberMember(geo, "latBottom");
            enc.lonLeft = numberMember(geo, "lonLeft");
            enc.lonRight = numberMember(geo, "lonRight");
        }
        const bool isOnGlobe =
            enc.latBottom >= -90.0 && enc.latBottom < enc.latTop && enc.latTop <= 90.0 &&
            enc.lonLeft >= -180.0 && enc.lonLeft < enc.lonRight && enc.lonRight <= 180.0;
        if (!isOnGlobe) {
            fail("'geo' is not a region of the globe between -180 and 180 degrees");
        }

        if (json.contains("classes") && json["classes"].is_array()) {
            for (const nlohmann::json& c : json["classes"]) {
                ValueClass vc;
                vc.code = codeMember(c, "code");
                vc.value = numberMember(c, "value");
                if (c.contains("label") && c["label"].is_string()) {
                    vc.label = c["label"].get<std::string>();
                }
                enc.classes.push_back(std::move(vc));
            }
        }

        const nlohmann::json& presentation = member(json, "presentation");
        const nlohmann::json& palette = member(presentation, "defaultPalette");
        const nlohmann::json& stops = member(palette, "stops");
        if (!stops.is_array() || stops.size() < 2) {
            fail("The palette needs at least two stops");
        }
        for (const nlohmann::json& s : stops) {
            const nlohmann::json& rgba = member(s, "rgba");
            if (!rgba.is_array() || rgba.size() != 4) {
                fail("A palette stop needs four 'rgba' components");
            }
            PaletteStop stop;
            stop.t = static_cast<float>(numberMember(s, "t"));
            for (int i = 0; i < 4; i++) {
                stop.rgba[i] = rgba[i].get<float>();
            }
            enc.palette.push_back(stop);
        }
        std::stable_sort(
            enc.palette.begin(),
            enc.palette.end(),
            [](const PaletteStop& a, const PaletteStop& b) { return a.t < b.t; }
        );

        if (presentation.contains("alphaMode") &&
            presentation["alphaMode"] == "gradient")
        {
            enc.alphaMode = ValueEncoding::AlphaMode::Gradient;
        }
        if (presentation.contains("transparentBelowValue") &&
            presentation["transparentBelowValue"].is_number())
        {
            enc.transparentBelowValue =
                presentation["transparentBelowValue"].get<double>();
        }
        if (presentation.contains("alphaGradient")) {
            const nlohmann::json& g = presentation["alphaGradient"];
            enc.rampStartValue = numberMember(g, "rampStartValue");
            enc.rampEndValue = numberMember(g, "rampEndValue");
            enc.rampGamma = g.contains("gamma") ? numberMember(g, "gamma") : 1.0;
        }
        return enc;
    }

    // The class whose code is closest to `code`; the first one wins a tie.
    const ValueClass& nearestClass(const ValueEncoding& encoding, int code) {
        const ValueClass* best = &encoding.classes.front();
        for (const ValueClass& c : encoding.classes) {
            if (std::abs(c.code - code) < std::abs(best->code - code)) {
                best = &c;
            }
        }
        return *best;
    }

    double paletteT(const ValueEncoding& encoding, int code) {
        if (encoding.kind == ValueEncoding::Kind::Classified &&
            !encoding.classes.empty())
        {
            code = nearestClass(encoding, code).code;
        }
        const double t = static_cast<double>(code - encoding.dataMinCode) /
            static_cast<double>(encoding.dataMaxCode - encoding.dataMinCode);
        return std::clamp(t, 0.0, 1.0);
    }

    glm::vec4 paletteAt(const std::vector<PaletteStop>& stops, double t) {
        size_t i = 0;
        while (i + 2 < stops.size() && stops[i + 1].t < t) {
            i++;
        }
        const PaletteStop& a = stops[i];
        const PaletteStop& b = i + 1 < stops.size() ? stops[i + 1] : a;
        const double span = b.t - a.t;
        const double f = span > 0.0 ? std::clamp((t - a.t) / span, 0.0, 1.0) : 0.0;
        return a.rgba + (b.rgba - a.rgba) * static_cast<float>(f);
    }

    double presentationAlpha(const ValueEncoding& encoding, double value) {
        // Not for a classified stream, whose values are class indices rather than the
        // physical units the floor is stated in
        if (encoding.kind != ValueEncoding::Kind::Classified &&
            encoding.transparentBelowValue.has_value() &&
            value < *encoding.transparentBelowValue)
        {
            return 0.0;
        }
        if (encoding.alphaMode != ValueEncoding::AlphaMode::Gradient) {
            return 1.0;
        }
        const double f = std::clamp(
            (value - encoding.rampStartValue) /
                (encoding.rampEndValue - encoding.rampStartValue),
            0.0,
            1.0
        );
        return std::pow(f, encoding.rampGamma);
    }

    // An EDL argument that may contain any character: `%<byte length>%<text>`.
    std::string edlQuoted(std::string_view text) {
        return std::format("%{}%{}", text.size(), text);
    }
} // namespace

namespace openspace::ayni {

std::string resolveUrl(std::string_view base, std::string_view relative) {
    const size_t schemeEnd = relative.find("://");
    if (schemeEnd != std::string_view::npos &&
        relative.find('/') > schemeEnd)
    {
        return std::string(relative);
    }

    const size_t baseSchemeEnd = base.find("://");
    if (baseSchemeEnd == std::string_view::npos) {
        fail(std::format("'{}' is not an absolute URL", base));
    }
    const size_t pathBegin = std::min(base.find('/', baseSchemeEnd + 3), base.size());
    const std::string_view origin = base.substr(0, pathBegin);
    std::string_view basePath = base.substr(pathBegin);
    const size_t queryBegin = std::min(basePath.find_first_of("?#"), basePath.size());
    basePath = basePath.substr(0, queryBegin);

    std::string path;
    if (relative.starts_with('/')) {
        path = relative;
    }
    else {
        const size_t lastSlash = basePath.rfind('/');
        path = lastSlash == std::string_view::npos ?
            "/" :
            std::string(basePath.substr(0, lastSlash + 1));
        path += relative;
    }
    return std::string(origin) + normalizePath(path);
}

std::string releaseUrlFromPointer(std::string_view pointerJson,
                                  std::string_view pointerUrl)
{
    const nlohmann::json json = nlohmann::json::parse(pointerJson, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        fail("The release pointer is not a JSON object");
    }
    if (stringMember(json, "schema") != PointerSchema) {
        fail(std::format("The release pointer is not '{}'", PointerSchema));
    }
    const std::string url = stringMember(member(json, "releaseDescriptor"), "url");
    return resolveUrl(pointerUrl, url);
}

Release parseRelease(std::string_view releaseJson, std::string_view releaseUrl) {
    const nlohmann::json json = nlohmann::json::parse(releaseJson, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        fail("The release descriptor is not a JSON object");
    }
    if (stringMember(json, "schema") != ReleaseSchema) {
        fail(std::format("The release descriptor is not '{}'", ReleaseSchema));
    }

    Release release;
    release.releaseId = stringMember(json, "releaseId");

    const nlohmann::json& timeRange = member(json, "timeRange");
    release.startTime = stringMember(timeRange, "start");
    release.endTime = stringMember(timeRange, "end");

    const nlohmann::json& representation = member(json, "representation");
    release.frameSize = glm::ivec2(
        static_cast<int>(numberMember(representation, "width")),
        static_cast<int>(numberMember(representation, "height"))
    );
    if (release.frameSize.x <= 0 || release.frameSize.y <= 0) {
        fail("The frame size must be positive");
    }

    release.initializationUrl = resolveUrl(
        releaseUrl,
        stringMember(member(json, "initialization"), "url")
    );

    const nlohmann::json& segments = member(json, "segments");
    if (!segments.is_array() || segments.empty()) {
        fail("The release has no segments");
    }
    for (const nlohmann::json& s : segments) {
        Segment segment;
        segment.url = resolveUrl(releaseUrl, stringMember(member(s, "media"), "url"));
        segment.durationSeconds = numberMember(s, "durationSeconds");
        if (!(segment.durationSeconds > 0.0)) {
            fail("A segment must have a positive duration");
        }
        release.segments.push_back(std::move(segment));
    }

    if (json.contains("valueEncoding")) {
        release.valueEncoding = parseEncoding(json["valueEncoding"], release.frameSize);
    }
    return release;
}

std::string openspaceDateTime(std::string_view iso) {
    const bool isValid = iso.size() >= 19 && iso[4] == '-' && iso[7] == '-' &&
        iso[10] == 'T' && iso[13] == ':' && iso[16] == ':';
    if (!isValid) {
        fail(std::format("'{}' is not an ISO 8601 time", iso));
    }
    return std::format(
        "{} {} {} {}",
        iso.substr(0, 4), iso.substr(5, 2), iso.substr(8, 2), iso.substr(11, 8)
    );
}

std::string edlPlaylist(const Release& release) {
    std::string edl = "# mpv EDL v0\n";
    edl += std::format("!mp4_dash,init={}\n", edlQuoted(release.initializationUrl));
    for (const Segment& segment : release.segments) {
        edl += std::format(
            "{},length={}\n",
            edlQuoted(segment.url), segment.durationSeconds
        );
    }
    return edl;
}

std::optional<double> valueAtCode(const ValueEncoding& encoding, int code) {
    if (code < encoding.nodataThresholdCode) {
        return std::nullopt;
    }
    if (encoding.kind == ValueEncoding::Kind::Classified &&
        !encoding.classes.empty())
    {
        return nearestClass(encoding, code).value;
    }
    const double t = paletteT(encoding, code);
    if (encoding.kind == ValueEncoding::Kind::Log) {
        return encoding.vmin * std::pow(encoding.vmax / encoding.vmin, t);
    }
    return encoding.vmin + t * (encoding.vmax - encoding.vmin);
}

std::array<glm::u8vec4, 256> paletteLookup(const ValueEncoding& encoding) {
    std::array<glm::u8vec4, 256> lookup = {};
    for (int code = 0; code < 256; code++) {
        const std::optional<double> value = valueAtCode(encoding, code);
        if (!value.has_value()) {
            // No data stays fully transparent
            continue;
        }
        glm::vec4 rgba = paletteAt(encoding.palette, paletteT(encoding, code));
        rgba.a *= static_cast<float>(presentationAlpha(encoding, *value));
        lookup[code] = glm::u8vec4(glm::round(glm::clamp(rgba, 0.f, 255.f)));
    }
    return lookup;
}

} // namespace openspace::ayni
