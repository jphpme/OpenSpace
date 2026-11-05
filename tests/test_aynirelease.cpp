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

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <modules/ayni/src/aynirelease.h>
#include <openspace/misc/exception.h>
#include <array>
#include <format>
#include <string>

using namespace openspace;
using namespace openspace::ayni;

namespace {
    constexpr std::string_view ReleaseUrl =
        "https://host.example/global/x/clouds/releases/r1/release.json";

    // A release in the shape the stream host publishes, cut down to two segments
    constexpr std::string_view ReleaseJson = R"({
      "schema": "realtime-new.immutable-dash-release.v1",
      "releaseId": "r1",
      "timeRange": { "start": "2026-08-25T07:00:00Z", "end": "2026-11-05T10:00:00Z" },
      "representation": { "width": 2048, "height": 1040 },
      "initialization": { "url": "../../init/a.mp4" },
      "segments": [
        { "durationSeconds": 1.5, "media": { "url": "../../segments/b.mp4" } },
        { "durationSeconds": 0.5, "media": { "url": "../../segments/c.mp4" } }
      ],
      "valueEncoding": {
        "kind": "luma8-linear",
        "units": "%",
        "vmin": 0.0,
        "vmax": 100.0,
        "nodataThresholdCode": 20,
        "dataMinCode": 32,
        "dataMaxCode": 235,
        "dataRegion": { "x": 0, "y": 0, "width": 2048, "height": 1024 },
        "presentation": {
          "defaultPalette": {
            "stops": [
              { "t": 1.0, "rgba": [255, 255, 255, 255] },
              { "t": 0.0, "rgba": [0, 0, 0, 255] }
            ]
          },
          "alphaMode": "gradient",
          "transparentBelowValue": 10,
          "alphaGradient": { "rampStartValue": 10, "rampEndValue": 90, "gamma": 1.0 }
        }
      }
    })";
} // namespace

TEST_CASE("AyniRelease: Resolve URL", "[aynirelease]") {
    constexpr std::string_view Base = "https://host.example/a/b/release.json";
    CHECK(resolveUrl(Base, "stream.mpd") == "https://host.example/a/b/stream.mpd");
    CHECK(resolveUrl(Base, "../../init/x.mp4") == "https://host.example/init/x.mp4");
    CHECK(resolveUrl(Base, "./c/../d.json") == "https://host.example/a/b/d.json");
    CHECK(resolveUrl(Base, "/root.json") == "https://host.example/root.json");
    CHECK(resolveUrl(Base, "https://other.example/x") == "https://other.example/x");
    CHECK(resolveUrl("https://host.example", "x.json") == "https://host.example/x.json");
    CHECK(
        resolveUrl("https://host.example/a/latest.json?v=1", "r/release.json") ==
        "https://host.example/a/r/release.json"
    );
    CHECK_THROWS_AS(resolveUrl("not a url", "x"), RuntimeError);
}

TEST_CASE("AyniRelease: Release pointer", "[aynirelease]") {
    constexpr std::string_view Pointer = R"({
      "schema": "realtime-new.release-pointer.v1",
      "releaseDescriptor": { "url": "releases/r1/release.json" }
    })";
    CHECK(
        releaseUrlFromPointer(Pointer, "https://host.example/clouds/latest.json") ==
        "https://host.example/clouds/releases/r1/release.json"
    );
    CHECK_THROWS_AS(
        releaseUrlFromPointer(R"({ "schema": "other" })", "https://host.example/l.json"),
        RuntimeError
    );
    CHECK_THROWS_AS(
        releaseUrlFromPointer("not json", "https://host.example/l.json"),
        RuntimeError
    );
}

TEST_CASE("AyniRelease: Parse release", "[aynirelease]") {
    const Release release = parseRelease(ReleaseJson, ReleaseUrl);
    CHECK(release.releaseId == "r1");
    CHECK(release.startTime == "2026-08-25T07:00:00Z");
    CHECK(release.endTime == "2026-11-05T10:00:00Z");
    CHECK(release.frameSize == glm::ivec2(2048, 1040));
    CHECK(release.initializationUrl == "https://host.example/global/x/clouds/init/a.mp4");
    REQUIRE(release.segments.size() == 2);
    CHECK(
        release.segments[0].url ==
        "https://host.example/global/x/clouds/segments/b.mp4"
    );
    CHECK(release.segments[1].durationSeconds == Catch::Approx(0.5));

    REQUIRE(release.valueEncoding.has_value());
    const ValueEncoding& enc = *release.valueEncoding;
    CHECK(enc.kind == ValueEncoding::Kind::Linear);
    CHECK(enc.units == "%");
    CHECK(enc.dataRegionSize == glm::ivec2(2048, 1024));
    REQUIRE(enc.palette.size() == 2);
    // The stops are sorted by their position
    CHECK(enc.palette[0].t == Catch::Approx(0.0));
    CHECK(enc.alphaMode == ValueEncoding::AlphaMode::Gradient);
    REQUIRE(enc.transparentBelowValue.has_value());
    CHECK(*enc.transparentBelowValue == Catch::Approx(10.0));
}

TEST_CASE("AyniRelease: Reject invalid releases", "[aynirelease]") {
    std::string json = std::string(ReleaseJson);
    const auto replaced = [&json](std::string_view from, std::string_view to) {
        std::string result = json;
        result.replace(result.find(from), from.size(), to);
        return result;
    };
    CHECK_THROWS_AS(
        parseRelease(replaced("immutable-dash-release", "other"), ReleaseUrl),
        RuntimeError
    );
    CHECK_THROWS_AS(
        parseRelease(replaced("luma8-linear", "luma16"), ReleaseUrl),
        RuntimeError
    );
    CHECK_THROWS_AS(
        parseRelease(replaced("\"height\": 1024", "\"height\": 2000"), ReleaseUrl),
        RuntimeError
    );
    CHECK_THROWS_AS(
        parseRelease(replaced("\"durationSeconds\": 1.5", "\"durationSeconds\": 0"),
        ReleaseUrl),
        RuntimeError
    );
}

TEST_CASE("AyniRelease: OpenSpace date time", "[aynirelease]") {
    CHECK(openspaceDateTime("2026-08-25T07:00:00Z") == "2026 08 25 07:00:00");
    CHECK(openspaceDateTime("2026-11-05T10:00:00.250Z") == "2026 11 05 10:00:00");
    CHECK_THROWS_AS(openspaceDateTime("2026 08 25"), RuntimeError);
}

TEST_CASE("AyniRelease: EDL playlist", "[aynirelease]") {
    const Release release = parseRelease(ReleaseJson, ReleaseUrl);
    const std::string init = "https://host.example/global/x/clouds/init/a.mp4";
    const std::string b = "https://host.example/global/x/clouds/segments/b.mp4";
    const std::string c = "https://host.example/global/x/clouds/segments/c.mp4";
    const std::string expected = std::format(
        "# mpv EDL v0\n!mp4_dash,init=%{}%{}\n%{}%{},length=1.5\n%{}%{},length=0.5\n",
        init.size(), init, b.size(), b, c.size(), c
    );
    CHECK(edlPlaylist(release) == expected);
}

TEST_CASE("AyniRelease: Values and palette", "[aynirelease]") {
    const Release release = parseRelease(ReleaseJson, ReleaseUrl);
    const ValueEncoding& enc = *release.valueEncoding;

    CHECK_FALSE(valueAtCode(enc, 19).has_value());
    // Codes between the no-data threshold and the bottom of the palette are clamped
    CHECK(*valueAtCode(enc, 20) == Catch::Approx(0.0));
    CHECK(*valueAtCode(enc, 32) == Catch::Approx(0.0));
    CHECK(*valueAtCode(enc, 235) == Catch::Approx(100.0));
    CHECK(*valueAtCode(enc, 255) == Catch::Approx(100.0));

    const std::array<glm::u8vec4, 256> lookup = paletteLookup(enc);
    // No data, and values below the transparency floor, are transparent
    CHECK(lookup[0] == glm::u8vec4(0));
    CHECK(lookup[40].a == 0);
    // The top of the palette is white and, at 100 %, fully opaque
    CHECK(lookup[235] == glm::u8vec4(255, 255, 255, 255));
    // Half way up the alpha ramp (50 %) with a linear gamma
    const int code = 32 + static_cast<int>((235 - 32) * 0.5 + 0.5);
    CHECK(lookup[code].a == 128);

    ValueEncoding logEncoding = enc;
    logEncoding.kind = ValueEncoding::Kind::Log;
    logEncoding.vmin = 1.0;
    logEncoding.vmax = 100.0;
    CHECK(*valueAtCode(logEncoding, 32) == Catch::Approx(1.0));
    CHECK(*valueAtCode(logEncoding, 235) == Catch::Approx(100.0));

    ValueEncoding classified = enc;
    classified.kind = ValueEncoding::Kind::Classified;
    classified.classes = {
        ValueClass { .code = 40, .value = 1.0, .label = "One" },
        ValueClass { .code = 80, .value = 2.0, .label = "Two" }
    };
    CHECK(*valueAtCode(classified, 55) == Catch::Approx(1.0));
    CHECK(*valueAtCode(classified, 61) == Catch::Approx(2.0));
}

#endif // OPENSPACE_MODULE_AYNI_ENABLED
