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

#ifndef __OPENSPACE_MODULE_AYNI___AYNICATALOGUE___H__
#define __OPENSPACE_MODULE_AYNI___AYNICATALOGUE___H__

#include <string>
#include <string_view>
#include <vector>

namespace openspace::ayni {

/// The address of the stream host's catalogue of streams
constexpr std::string_view CatalogueUrl = "https://pachamama-studios.stream/index.json";

/// A stream from the stream host's catalogue that the AYNI stream layer can play
struct CatalogueStream {
    /// The stream's name in the catalogue, such as `clouds`
    std::string identifier;
    /// The name shown to people, such as "Global Cloud Cover"
    std::string name;
    std::string description;
    /// The stream's first category, capitalized, such as "Atmosphere"
    std::string category;
    /// The scene graph node of the globe the stream covers, such as `Earth` or `Mars`
    std::string globe;
    /// The address of the stream's release pointer (its `latest.json`)
    std::string url;
};

/**
 * Parses the stream host's catalogue and returns the streams that the AYNI stream layer
 * can play: the value-encoded streams that cover a whole globe. Entries that are
 * incomplete, or whose release pointer is not on the stream host, are left out.
 *
 * \param json The contents of the catalogue
 * \return The playable streams, in the order of the catalogue
 *
 * \throw RuntimeError If \p json is not a catalogue
 */
std::vector<CatalogueStream> parseCatalogue(std::string_view json);

/// Returns the identifier of the globe layer that shows \p stream.
std::string layerIdentifier(const CatalogueStream& stream);

/// Returns whether \p identifier is the identifier of a globe layer that shows a stream.
bool isStreamLayer(std::string_view identifier);

} // namespace openspace::ayni

#endif // __OPENSPACE_MODULE_AYNI___AYNICATALOGUE___H__
