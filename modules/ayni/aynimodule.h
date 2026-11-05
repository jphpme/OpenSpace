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

#ifndef __OPENSPACE_MODULE_AYNI___AYNIMODULE___H__
#define __OPENSPACE_MODULE_AYNI___AYNIMODULE___H__

#include <openspace/util/openspacemodule.h>

#include <modules/ayni/src/aynicatalogue.h>
#include <openspace/properties/list/stringlistproperty.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openspace {

namespace ayni { class StreamAccess; }

class AyniModule : public OpenSpaceModule {
public:
    constexpr static const char* Name = "Ayni";

    AyniModule();
    ~AyniModule() override;

    std::vector<openspace::Documentation> documentations() const override;

    /**
     * Returns the stream token holder that all AYNI layers share. It is created, and the
     * connection file read, on the first call.
     *
     * \throw RuntimeError If the connection file cannot be used
     */
    ayni::StreamAccess& streamAccess();

    /**
     * Returns the streams of the stream host's catalogue that can be played. The
     * catalogue is read on the first call; this call blocks while it is read.
     *
     * \throw RuntimeError If the catalogue cannot be read
     */
    const std::vector<ayni::CatalogueStream>& catalogue();

    /// Returns the identifiers of the streams that `openspace.ayni.stepStream` shows
    std::vector<std::string> playlist() const;

    /// Returns the identifier of the stream shown last, or an empty string
    const std::string& shownStream() const;
    void setShownStream(std::string identifier);

    LuaLibrary luaLibrary() const override;

protected:
    void internalInitialize(const Dictionary& dict) override;
    void internalDeinitialize() override;

private:
    StringListProperty _playlist;

    std::unique_ptr<ayni::StreamAccess> _streamAccess;
    std::optional<std::vector<ayni::CatalogueStream>> _catalogue;
    std::string _shownStream;
};

} // namespace openspace

#endif // __OPENSPACE_MODULE_AYNI___AYNIMODULE___H__
