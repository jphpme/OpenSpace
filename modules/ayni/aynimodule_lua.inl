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

#include <modules/ayni/src/aynicatalogue.h>
#include <modules/globebrowsing/src/layer.h>
#include <modules/globebrowsing/src/layergroup.h>
#include <modules/globebrowsing/src/layergroupid.h>
#include <modules/globebrowsing/src/layermanager.h>
#include <modules/globebrowsing/src/renderableglobe.h>
#include <openspace/engine/globals.h>
#include <openspace/engine/moduleengine.h>
#include <openspace/lua/lua_helper.h>
#include <openspace/misc/dictionary.h>
#include <openspace/misc/exception.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/scene/scene.h>
#include <openspace/scene/scenegraphnode.h>
#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace {
    using namespace openspace;

    AyniModule& ayniModule() {
        return *global::moduleEngine->module<AyniModule>();
    }

    const std::vector<ayni::CatalogueStream>& catalogue() {
        try {
            return ayniModule().catalogue();
        }
        catch (const RuntimeError& e) {
            throw lua::LuaError(e.message);
        }
    }

    RenderableGlobe* globeNamed(const std::string& identifier) {
        SceneGraphNode* node = global::renderEngine->scene()->sceneGraphNode(identifier);
        return node ? dynamic_cast<RenderableGlobe*>(node->renderable()) : nullptr;
    }

    void removeStreamLayers() {
        constexpr layers::Group::ID Group = layers::Group::ID::ColorLayers;
        for (SceneGraphNode* node : global::renderEngine->scene()->allSceneGraphNodes()) {
            RenderableGlobe* globe = dynamic_cast<RenderableGlobe*>(node->renderable());
            if (!globe) {
                continue;
            }
            std::vector<std::string> identifiers;
            for (const Layer* layer : globe->layerManager().layerGroup(Group).layers()) {
                if (ayni::isStreamLayer(layer->identifier())) {
                    identifiers.push_back(layer->identifier());
                }
            }
            for (const std::string& identifier : identifiers) {
                globe->layerManager().deleteLayer(Group, identifier);
            }
        }
    }

    std::string show(const ayni::CatalogueStream& stream) {
        RenderableGlobe* globe = globeNamed(stream.globe);
        if (!globe) {
            throw lua::LuaError(std::format(
                "The stream '{}' covers {}, which is not in the scene",
                stream.identifier, stream.globe
            ));
        }

        // A stream that is already shown keeps playing where it is
        const std::string identifier = ayni::layerIdentifier(stream);
        const LayerGroup& group =
            globe->layerManager().layerGroup(layers::Group::ID::ColorLayers);
        if (group.hasPropertySubOwner(identifier)) {
            ayniModule().setShownStream(stream.identifier);
            return stream.name;
        }
        removeStreamLayers();

        Dictionary layer;
        layer.setValue("Identifier", identifier);
        layer.setValue("Name", stream.name);
        layer.setValue("Description", stream.description);
        layer.setValue("Type", std::string("AyniStreamTileProvider"));
        layer.setValue("Url", stream.url);
        layer.setValue("Enabled", true);
        layer.setValue("GlobeName", stream.globe);
        Layer* added = globe->layerManager().addLayer(
            layers::Group::ID::ColorLayers,
            layer
        );
        if (!added) {
            throw lua::LuaError(std::format(
                "Could not show the stream '{}'; the log says why", stream.identifier
            ));
        }
        added->initialize();
        ayniModule().setShownStream(stream.identifier);
        return stream.name;
    }

/**
 * Returns the AYNI streams that OpenSpace can play: the value-encoded streams of the
 * stream host's catalogue that cover a whole globe. Each is a table with the keys
 * `Identifier`, `Name`, `Description`, `Category`, `Globe` (the scene graph node of the
 * globe it covers) and `Url`. The catalogue is read from the stream host on the first
 * call.
 */
[[codegen::luawrap]] std::vector<Dictionary> streams() {
    std::vector<Dictionary> result;
    for (const ayni::CatalogueStream& stream : catalogue()) {
        Dictionary entry;
        entry.setValue("Identifier", stream.identifier);
        entry.setValue("Name", stream.name);
        entry.setValue("Description", stream.description);
        entry.setValue("Category", stream.category);
        entry.setValue("Globe", stream.globe);
        entry.setValue("Url", stream.url);
        result.push_back(std::move(entry));
    }
    return result;
}

/**
 * Shows an AYNI stream on the globe it covers and removes the AYNI stream shown before,
 * so that one stream plays at a time. Licensed streams need the OpenSpace connection
 * file of an AYNI XR plan in `${USER}/ayni/connection.json`.
 *
 * \param identifier The stream's identifier, as `openspace.ayni.streams` lists it
 * \return The name of the stream
 */
[[codegen::luawrap]] std::string showStream(std::string identifier) {
    const std::vector<ayni::CatalogueStream>& streams = catalogue();
    const auto it = std::find_if(
        streams.begin(),
        streams.end(),
        [&identifier](const ayni::CatalogueStream& s) {
            return s.identifier == identifier;
        }
    );
    if (it == streams.end()) {
        throw lua::LuaError(std::format("There is no AYNI stream '{}'", identifier));
    }
    return show(*it);
}

/**
 * Removes the AYNI stream from the globe it is shown on.
 */
[[codegen::luawrap]] void hideStreams() {
    removeStreamLayers();
    ayniModule().setShownStream("");
}

/**
 * Shows the stream after the one shown last in the playlist (`Modules.Ayni.Playlist`),
 * or before it if \p step is negative, and removes the stream shown before. Streams whose
 * globe is not in the scene are skipped. If the playlist is empty, every stream of the
 * catalogue takes its turn.
 *
 * \param step How many streams to move forward, or back if negative
 * \return The name of the stream that is shown
 */
[[codegen::luawrap]] std::string stepStream(int step = 1) {
    const std::vector<ayni::CatalogueStream>& streams = catalogue();
    const std::vector<std::string> playlist = ayniModule().playlist();

    // In the playlist's order, or the catalogue's for an empty playlist
    std::vector<const ayni::CatalogueStream*> candidates;
    if (playlist.empty()) {
        for (const ayni::CatalogueStream& stream : streams) {
            candidates.push_back(&stream);
        }
    }
    else {
        for (const std::string& identifier : playlist) {
            const auto it = std::find_if(
                streams.begin(),
                streams.end(),
                [&identifier](const ayni::CatalogueStream& s) {
                    return s.identifier == identifier;
                }
            );
            if (it != streams.end()) {
                candidates.push_back(&*it);
            }
        }
    }
    std::erase_if(
        candidates,
        [](const ayni::CatalogueStream* s) { return !globeNamed(s->globe); }
    );
    if (candidates.empty()) {
        throw lua::LuaError("None of the AYNI streams in the playlist can be shown");
    }

    const int count = static_cast<int>(candidates.size());
    const auto shown = std::find_if(
        candidates.begin(),
        candidates.end(),
        [](const ayni::CatalogueStream* s) {
            return s->identifier == ayniModule().shownStream();
        }
    );
    int index = step < 0 ? count - 1 : 0;
    if (shown != candidates.end()) {
        const int current = static_cast<int>(shown - candidates.begin());
        index = ((current + step) % count + count) % count;
    }
    return show(*candidates[static_cast<size_t>(index)]);
}

} // namespace

#include "aynimodule_lua_codegen.cpp"
