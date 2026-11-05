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
#include <openspace/misc/assert.h>
#include <openspace/misc/templatefactory.h>
#include <openspace/util/factorymanager.h>

namespace openspace {

AyniModule::AyniModule()
    : OpenSpaceModule(AyniModule::Name)
{}

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

std::vector<Documentation> AyniModule::documentations() const {
    return {
        AyniStreamTileProvider::Documentation()
    };
}

} // namespace openspace
