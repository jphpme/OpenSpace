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

#include <modules/ayni/src/aynilicence.h>

using namespace openspace::ayni;

TEST_CASE("AyniLicence: Normalize key", "[aynilicence]") {
    constexpr std::string_view Formatted = "AYNI-01234-56789-ABCDE-FGHJH";
    CHECK(normalizeLicenceKey(Formatted) == Formatted);
    // As people type or paste it
    CHECK(normalizeLicenceKey("ayni-01234-56789-abcde-fghjh\n") == Formatted);
    CHECK(normalizeLicenceKey("0123456789ABCDEFGHJH") == Formatted);
    CHECK(normalizeLicenceKey("O1234 56789 ABCDE FGHJH") == Formatted);
    CHECK(
        normalizeLicenceKey("AYNI-K7M2Q-9X4T1-W8B3N-6P5R3") ==
        "AYNI-K7M2Q-9X4T1-W8B3N-6P5R3"
    );
    CHECK(
        normalizeLicenceKey("ZZZZZZZZZZZZZZZZZZZ0") == "AYNI-ZZZZZ-ZZZZZ-ZZZZZ-ZZZZ0"
    );

    // A wrong check character, a wrong length, or a letter outside the alphabet
    CHECK_FALSE(normalizeLicenceKey("AYNI-01234-56789-ABCDE-FGHJK").has_value());
    CHECK_FALSE(normalizeLicenceKey("AYNI-01234-56789-ABCDE-FGHJ").has_value());
    CHECK_FALSE(normalizeLicenceKey("U123456789ABCDEFGHJH").has_value());
    CHECK_FALSE(normalizeLicenceKey("").has_value());
}

TEST_CASE("AyniLicence: Stream host", "[aynilicence]") {
    CHECK(isOnStreamHost("https://pachamama-studios.stream/global/x/latest.json"));
    CHECK_FALSE(isOnStreamHost("http://pachamama-studios.stream/global/x/latest.json"));
    CHECK_FALSE(isOnStreamHost("https://pachamama-studios.stream.example.com/x"));
    CHECK_FALSE(isOnStreamHost("https://example.com/pachamama-studios.stream/x"));
}

#endif // OPENSPACE_MODULE_AYNI_ENABLED
