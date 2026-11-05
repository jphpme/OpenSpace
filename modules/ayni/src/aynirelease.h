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

#ifndef __OPENSPACE_MODULE_AYNI___AYNIRELEASE___H__
#define __OPENSPACE_MODULE_AYNI___AYNIRELEASE___H__

#include <openspace/glm.h>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openspace::ayni {

/// One stop of a release's palette; `t` is the palette position in [0, 1].
struct PaletteStop {
    float t = 0.f;
    glm::vec4 rgba = glm::vec4(0.f);
};

/// One class of a classified stream: the code it is drawn with and the value it means.
struct ValueClass {
    int code = 0;
    double value = 0.0;
    std::string label;
};

/**
 * How the luma codes of a value-encoded release stand for measurements (the
 * `valueEncoding` block of a `release.json`). Codes below `nodataThresholdCode` are no
 * data, `dataMinCode` to `dataMaxCode` span the palette, and the value behind a code is
 * linear or logarithmic between `vmin` and `vmax`, or the value of the nearest class.
 */
struct ValueEncoding {
    enum class Kind {
        Linear,
        Log,
        Classified
    };
    enum class AlphaMode {
        Opaque,
        Gradient
    };

    Kind kind = Kind::Linear;
    int nodataThresholdCode = 0;
    int dataMinCode = 0;
    int dataMaxCode = 255;
    double vmin = 0.0;
    double vmax = 1.0;
    std::string units;

    /// The rectangle of the frame that holds the map, in pixels from the top left
    glm::ivec2 dataRegionOffset = glm::ivec2(0);
    glm::ivec2 dataRegionSize = glm::ivec2(0);

    std::vector<PaletteStop> palette;
    std::vector<ValueClass> classes;

    AlphaMode alphaMode = AlphaMode::Opaque;
    std::optional<double> transparentBelowValue;
    double rampStartValue = 0.0;
    double rampEndValue = 1.0;
    double rampGamma = 1.0;
};

/// One media segment of a release; `url` is absolute.
struct Segment {
    std::string url;
    double durationSeconds = 0.0;
};

/**
 * An immutable release of an AYNI stream (`realtime-new.immutable-dash-release.v1`): the
 * dates it covers, its fragmented MP4 initialization and media segments, and how its
 * frames encode values.
 */
struct Release {
    std::string releaseId;
    /// ISO 8601 start of the first frame
    std::string startTime;
    /// ISO 8601 end of the last frame (exclusive)
    std::string endTime;
    glm::ivec2 frameSize = glm::ivec2(0);
    std::string initializationUrl;
    std::vector<Segment> segments;
    std::optional<ValueEncoding> valueEncoding;
};

/**
 * Resolves \p relative against \p base the way a browser resolves a link: an absolute
 * URL is returned as is, a path starting with `/` replaces the path of \p base, and
 * anything else replaces the last path segment of \p base, with `.` and `..` segments
 * removed.
 */
std::string resolveUrl(std::string_view base, std::string_view relative);

/**
 * Returns the absolute URL of the `release.json` that the release pointer \p pointerJson
 * (a stream's `latest.json`, read from \p pointerUrl) names.
 */
std::string releaseUrlFromPointer(std::string_view pointerJson,
    std::string_view pointerUrl);

/**
 * Parses the `release.json` \p releaseJson that was read from \p releaseUrl. Segment URLs
 * are made absolute.
 *
 * \throw RuntimeError If the descriptor is not a release this module can play
 */
Release parseRelease(std::string_view releaseJson, std::string_view releaseUrl);

/**
 * Converts the ISO 8601 UTC time \p iso (`2026-08-25T07:00:00Z`) into the form
 * `2026 08 25 07:00:00` that OpenSpace's date verifiers accept.
 *
 * \throw RuntimeError If \p iso does not start with `YYYY-MM-DDThh:mm:ss`
 */
std::string openspaceDateTime(std::string_view iso);

/**
 * Builds an mpv EDL playlist (`# mpv EDL v0`) that plays the segments of \p release as
 * one fragmented MP4 stream. Each segment carries its length, so mpv knows the duration
 * and seeks without reading every segment.
 */
std::string edlPlaylist(const Release& release);

/**
 * The colour, as RGBA in [0, 255], for every luma code: transparent for no data, else the
 * palette colour at the code's palette position with the presentation's alpha applied.
 * This matches what the other AYNI players draw.
 */
std::array<glm::u8vec4, 256> paletteLookup(const ValueEncoding& encoding);

/// The measurement that \p code stands for, or no value for no data.
std::optional<double> valueAtCode(const ValueEncoding& encoding, int code);

} // namespace openspace::ayni

#endif // __OPENSPACE_MODULE_AYNI___AYNIRELEASE___H__
