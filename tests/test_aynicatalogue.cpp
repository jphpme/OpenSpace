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

#include <modules/ayni/src/aynicatalogue.h>
#include <openspace/misc/exception.h>

using namespace openspace;
using namespace openspace::ayni;

namespace {
    // Entries shaped as the stream host's `index.json` has them
    constexpr std::string_view Catalogue = R"({
      "schemaVersion": 1,
      "datasets": [
        {
          "name": "clouds", "display_name": "Global Cloud Cover",
          "description": "Clouds.", "type": "stream", "valueEncoded": true,
          "coverage": "global", "categories": ["atmosphere", "climate"],
          "releaseDescriptorUrl": "global/realtime/noaa-sos/clouds/latest.json"
        },
        {
          "name": "mars_albedo_tes", "display_name": "Mars Albedo",
          "type": "stream", "valueEncoded": true, "coverage": "global",
          "celestialBody": "mars",
          "releaseDescriptorUrl": "global/historical/nasa-pds/mars_albedo_tes/latest.json"
        },
        {
          "name": "aemet_precip_obs", "display_name": "Regional",
          "type": "stream", "valueEncoded": true, "coverage": "regional",
          "releaseDescriptorUrl": "regional/spain/x/latest.json"
        },
        {
          "name": "pictures", "display_name": "Not value-encoded",
          "type": "stream", "valueEncoded": false, "coverage": "global",
          "releaseDescriptorUrl": "global/x/latest.json"
        },
        {
          "name": "elsewhere", "display_name": "Another host",
          "type": "stream", "valueEncoded": true, "coverage": "global",
          "releaseDescriptorUrl": "https://example.com/latest.json"
        },
        {
          "name": "up", "display_name": "Out of the host's folder",
          "type": "stream", "valueEncoded": true, "coverage": "global",
          "releaseDescriptorUrl": "../latest.json"
        },
        {
          "name": "bad name", "display_name": "A name that is no identifier",
          "type": "stream", "valueEncoded": true, "coverage": "global",
          "releaseDescriptorUrl": "global/y/latest.json"
        }
      ]
    })";
} // namespace

TEST_CASE("AyniCatalogue: Playable streams", "[aynicatalogue]") {
    const std::vector<CatalogueStream> streams = parseCatalogue(Catalogue);
    REQUIRE(streams.size() == 2);

    CHECK(streams[0].identifier == "clouds");
    CHECK(streams[0].name == "Global Cloud Cover");
    CHECK(streams[0].description == "Clouds.");
    CHECK(streams[0].category == "Atmosphere");
    CHECK(streams[0].globe == "Earth");
    CHECK(
        streams[0].url ==
        "https://pachamama-studios.stream/global/realtime/noaa-sos/clouds/latest.json"
    );

    // Another body, and no categories
    CHECK(streams[1].identifier == "mars_albedo_tes");
    CHECK(streams[1].globe == "Mars");
    CHECK(streams[1].category == "Other");
}

TEST_CASE("AyniCatalogue: Not a catalogue", "[aynicatalogue]") {
    CHECK_THROWS_AS(parseCatalogue(""), RuntimeError);
    CHECK_THROWS_AS(parseCatalogue(R"({ "datasets": 1 })"), RuntimeError);
    CHECK(parseCatalogue(R"({ "datasets": [] })").empty());
}

TEST_CASE("AyniCatalogue: Layer identifiers", "[aynicatalogue]") {
    const CatalogueStream stream = { .identifier = "clouds" };
    CHECK(layerIdentifier(stream) == "AyniStream_clouds");
    CHECK(isStreamLayer(layerIdentifier(stream)));
    CHECK_FALSE(isStreamLayer("ESRI_World_Imagery"));
}

#endif // OPENSPACE_MODULE_AYNI_ENABLED
