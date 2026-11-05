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

#ifndef __OPENSPACE_MODULE_AYNI___AYNILICENCE___H__
#define __OPENSPACE_MODULE_AYNI___AYNILICENCE___H__

#include <optional>
#include <string>
#include <string_view>

namespace openspace::ayni {

/// The AYNI stream host; the only host that a licence token is ever sent to
constexpr std::string_view StreamHost = "https://pachamama-studios.stream/";

/// Returns whether \p url is a URL on the AYNI stream host.
bool isOnStreamHost(std::string_view url);

/**
 * Normalizes a licence key the way people type it: case, separators, an `AYNI` prefix,
 * and the letters `O`, `I` and `L` read as `0` and `1`. Returns the key in the form
 * `AYNI-XXXXX-XXXXX-XXXXX-XXXXX`, or no value if \p input is not a key or its check
 * character does not match.
 */
std::optional<std::string> normalizeLicenceKey(std::string_view input);

/**
 * Returns the content token that this installation sends to the stream host, or no value
 * if there is no licence key in `${USER}/ayni/licence.key`. The first call activates the
 * key with the licence server; afterwards the token stored in `${USER}/ayni/licence.json`
 * is used, and refreshed once it is older than 20 hours. This call blocks while it talks
 * to the licence server.
 *
 * \throw RuntimeError If the key is invalid or the licence server does not accept it
 */
std::optional<std::string> streamToken();

} // namespace openspace::ayni

#endif // __OPENSPACE_MODULE_AYNI___AYNILICENCE___H__
