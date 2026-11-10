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

#include <modules/ayni/aynimodule.h>

#include <modules/ayni/src/ayniconnection.h>
#include <modules/ayni/src/aynistreamtileprovider.h>
#include <modules/globebrowsing/src/tileprovider/tileprovider.h>
#include <openspace/documentation/documentation.h>
#include <openspace/logging/logmanager.h>
#include <openspace/misc/assert.h>
#include <openspace/misc/exception.h>
#include <openspace/misc/templatefactory.h>
#include <openspace/scripting/lualibrary.h>
#include <openspace/util/factorymanager.h>
#include <openspace/util/httprequest.h>
#include <array>
#include <chrono>
#include <format>
#include <optional>
#include <string_view>

#include "aynimodule_lua.inl"

namespace {
    constexpr std::string_view _loggerCat = "AyniModule";

    // Realtime Earth streams that show what AYNI streams are
    constexpr std::array<std::string_view, 7> DefaultPlaylist = {
        "clouds", "aod550", "ostia_sst_nrt", "imerg_precip_nrt", "land_surface_temp",
        "firms_fires", "surface_o3"
    };

    constexpr openspace::StringListPropertyInfo PlaylistInfo = {
        .identifier = "Playlist",
        .name = "Playlist",
        .description = "The streams, by their identifier in the stream host's "
            "catalogue, that the next and previous stream actions show in turn. Streams "
            "that are not in the catalogue, or whose globe is not in the scene, are "
            "skipped. If the list is empty, the actions go through every stream.",
        .visibility = openspace::Visibility::User
    };
} // namespace

namespace openspace {

AyniModule::AyniModule()
    : OpenSpaceModule(AyniModule::Name)
    , _playlist(PlaylistInfo)
{
    _playlist = std::vector<std::string>(DefaultPlaylist.begin(), DefaultPlaylist.end());
    addProperty(_playlist);
}

AyniModule::~AyniModule() = default;

void AyniModule::internalInitialize(const Dictionary&) {
    TemplateFactory<TileProvider>* fTileProvider =
        FactoryManager::ref().factory<TileProvider>();
    assert_msg(fTileProvider, "TileProvider factory was not created");
    fTileProvider->registerClass<AyniStreamTileProvider>("AyniStreamTileProvider");
}

void AyniModule::internalDeinitialize() {
    _streamAccess = nullptr;
}

ayni::StreamAccess& AyniModule::streamAccess() {
    if (!_streamAccess) {
        _streamAccess = std::make_unique<ayni::StreamAccess>();
    }
    return *_streamAccess;
}

const std::vector<ayni::CatalogueStream>& AyniModule::catalogue() {
    if (!_catalogue.has_value()) {
        HttpMemoryDownload request = HttpMemoryDownload(std::string(ayni::CatalogueUrl));
        // The stream host lists its streams only to readers with a licence
        const std::optional<std::string> token = streamAccess().token();
        if (token.has_value()) {
            request.addRequestHeader(ayni::authorizationHeader(*token));
        }
        request.start(std::chrono::seconds(30));
        if (!request.wait()) {
            const std::string_view hint = token.has_value() ?
                "" :
                ". The catalogue needs an AYNI XR connection file in "
                "${USER}/ayni/connection.json";
            throw RuntimeError(
                std::format(
                    "Could not read the catalogue '{}'{}", ayni::CatalogueUrl, hint
                ),
                std::string(_loggerCat)
            );
        }
        const std::vector<char>& data = request.downloadedData();
        _catalogue = ayni::parseCatalogue(std::string_view(data.data(), data.size()));
        LINFO(std::format("The AYNI catalogue has {} streams", _catalogue->size()));
    }
    return *_catalogue;
}

std::vector<std::string> AyniModule::playlist() const {
    return _playlist;
}

const std::string& AyniModule::shownStream() const {
    return _shownStream;
}

void AyniModule::setShownStream(std::string identifier) {
    _shownStream = std::move(identifier);
}

std::vector<Documentation> AyniModule::documentations() const {
    return {
        AyniStreamTileProvider::Documentation()
    };
}

LuaLibrary AyniModule::luaLibrary() const {
    return {
        .name = "ayni",
        .functions = {
            codegen::lua::Streams,
            codegen::lua::ShowStream,
            codegen::lua::HideStreams,
            codegen::lua::StepStream
        }
    };
}

} // namespace openspace
