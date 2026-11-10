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

    // Without a `geo` block, the map covers the whole globe
    CHECK(enc.latTop == Catch::Approx(90.0));
    CHECK(enc.latBottom == Catch::Approx(-90.0));
    CHECK(enc.lonLeft == Catch::Approx(-180.0));
    CHECK(enc.lonRight == Catch::Approx(180.0));
}

TEST_CASE("AyniRelease: Regional release", "[aynirelease]") {
    constexpr std::string_view DataRegion =
        R"("dataRegion": { "x": 0, "y": 0, "width": 2048, "height": 1024 },)";
    const auto withGeo = [&DataRegion](std::string_view geo) {
        std::string json = std::string(ReleaseJson);
        const size_t at = json.find(DataRegion);
        json.insert(at + DataRegion.size(), std::format(R"( "geo": {{ {} }},)", geo));
        return json;
    };

    // AEMET's forecast for Spain
    const Release release = parseRelease(
        withGeo(R"("latTop": 44.4875, "latBottom": 25.4875, "lonLeft": -20.0125, )"
                R"("lonRight": 4.9875, "pixelCenter": true)"),
        ReleaseUrl
    );
    REQUIRE(release.valueEncoding.has_value());
    CHECK(release.valueEncoding->latTop == Catch::Approx(44.4875));
    CHECK(release.valueEncoding->latBottom == Catch::Approx(25.4875));
    CHECK(release.valueEncoding->lonLeft == Catch::Approx(-20.0125));
    CHECK(release.valueEncoding->lonRight == Catch::Approx(4.9875));

    // Edges in the wrong order, off the globe, or across the antimeridian
    CHECK_THROWS_AS(
        parseRelease(
            withGeo(R"("latTop": 10, "latBottom": 20, "lonLeft": 0, "lonRight": 5)"),
            ReleaseUrl
        ),
        RuntimeError
    );
    CHECK_THROWS_AS(
        parseRelease(
            withGeo(R"("latTop": 95, "latBottom": 20, "lonLeft": 0, "lonRight": 5)"),
            ReleaseUrl
        ),
        RuntimeError
    );
    CHECK_THROWS_AS(
        parseRelease(
            withGeo(R"("latTop": 60, "latBottom": 50, "lonLeft": 170, "lonRight": -170)"),
            ReleaseUrl
        ),
        RuntimeError
    );
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

TEST_CASE("AyniRelease: Segment lengths from the MPD", "[aynirelease]") {
    // The releases of still pictures list bare segment addresses and an MPD
    std::string json = std::string(ReleaseJson);
    for (const std::string_view length : { "1.5", "0.5" }) {
        const std::string media =
            std::format(R"("durationSeconds": {}, "media": {{ "url")", length);
        json.replace(json.find(media), media.size(), R"("url")");
    }
    // and the brace that closed the `media` block
    for (int i = 0; i < 2; i++) {
        json.replace(json.find(".mp4\" } }"), 9, ".mp4\" }");
    }

    // Without an MPD the lengths are missing
    CHECK_THROWS_AS(parseRelease(json, ReleaseUrl), RuntimeError);

    json.insert(json.find("\"segments\""), R"("mpd": { "url": "stream.mpd" }, )");
    Release release = parseRelease(json, ReleaseUrl);
    CHECK(release.mpdUrl == "https://host.example/global/x/clouds/releases/r1/stream.mpd");
    REQUIRE(release.segments.size() == 2);
    CHECK(
        release.segments[1].url ==
        "https://host.example/global/x/clouds/segments/c.mp4"
    );
    CHECK(release.segments[0].durationSeconds == 0.0);

    // The timeline as the stream host writes it, with a repeat
    constexpr std::string_view Mpd = R"(<?xml version='1.0' encoding='utf-8'?>
      <MPD type="static" mediaPresentationDuration="PT4.0S">
        <Period id="0">
          <AdaptationSet id="0" contentType="video" frameRate="12/1">
            <Representation id="video-0" width="720" height="376">
              <SegmentList timescale="12000">
                <Initialization sourceURL="../../init/a.mp4" />
                <SegmentTimeline>
                  <S d="24000" t="0" r="1" />
                </SegmentTimeline>
                <SegmentURL media="../../segments/b.mp4" />
                <SegmentURL media="../../segments/c.mp4" />
              </SegmentList>
            </Representation>
          </AdaptationSet>
        </Period>
      </MPD>)";
    setSegmentDurations(release, Mpd);
    CHECK(release.segments[0].durationSeconds == Catch::Approx(2.0));
    CHECK(release.segments[1].durationSeconds == Catch::Approx(2.0));

    // One segment of 1 s, the shape of a single still picture
    constexpr std::string_view StillMpd = R"(<SegmentList timescale="1000">
        <SegmentTimeline><S t="0" d="1000"/></SegmentTimeline></SegmentList>)";
    Release still = release;
    still.segments.resize(1);
    setSegmentDurations(still, StillMpd);
    CHECK(still.segments[0].durationSeconds == Catch::Approx(1.0));

    // A timeline that does not match the release, or is not one
    CHECK_THROWS_AS(setSegmentDurations(release, StillMpd), RuntimeError);
    CHECK_THROWS_AS(setSegmentDurations(release, "<MPD></MPD>"), RuntimeError);
    CHECK_THROWS_AS(
        setSegmentDurations(
            still,
            "<SegmentTimeline><S d=\"-5\"/></SegmentTimeline>"
        ),
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
