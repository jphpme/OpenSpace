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

#include <modules/ayni/src/aynistreamtileprovider.h>

#include <modules/ayni/aynimodule.h>
#include <modules/ayni/src/ayniconnection.h>
#include <modules/globebrowsing/src/basictypes.h>
#include <modules/globebrowsing/src/tileindex.h>
#include <openspace/documentation/documentation.h>
#include <openspace/engine/globals.h>
#include <openspace/engine/moduleengine.h>
#include <openspace/engine/windowdelegate.h>
#include <openspace/filesystem/filesystem.h>
#include <openspace/logging/logmanager.h>
#include <openspace/misc/dictionary.h>
#include <openspace/misc/exception.h>
#include <openspace/misc/profiling.h>
#include <openspace/opengl/framebufferobject.h>
#include <openspace/opengl/openglstatecache.h>
#include <openspace/opengl/programobject.h>
#include <openspace/opengl/texture.h>
#include <openspace/opengl/textureunit.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/util/httprequest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>

namespace {
    using namespace openspace;

    constexpr std::string_view _loggerCat = "AyniStreamTileProvider";

    // Plays a value-encoded AYNI stream on a globe, mapped to the simulation time that
    // the stream's release covers. The frames carry measurements rather than pictures;
    // they are coloured with the palette the release declares, codes that mean no data
    // are transparent, and the calibration strip at the edge of every frame is cropped
    // off. Licensed streams need a connection file of an AYNI XR plan, for OpenSpace or
    // for a node, in `${USER}/ayni/connection.json`.
    struct [[codegen::Dictionary(AyniStreamTileProvider)]] Parameters {
        // The URL of the stream's release pointer (its `latest.json`) or of one of its
        // releases (a `release.json`). The pointer is read once, when the layer is
        // created.
        std::string url;
    };

    ayni::StreamAccess& streamAccess() {
        return global::moduleEngine->module<AyniModule>()->streamAccess();
    }

    std::string download(const std::string& url,
                         const std::optional<std::string>& token)
    {
        // @TODO (2026-11-05, jphpme) This blocks while the asset loads; move the release
        // lookup off the main thread once streams refresh while running
        HttpMemoryDownload request = HttpMemoryDownload(url);
        if (token.has_value() && ayni::isOnStreamHost(url)) {
            request.addRequestHeader(ayni::authorizationHeader(*token));
        }
        request.start();
        const bool success = request.wait();
        const std::vector<char>& data = request.downloadedData();
        if (!success) {
            const std::string hint = token.has_value() || !ayni::isOnStreamHost(url) ?
                "" :
                ". Licensed streams need an AYNI XR connection file in "
                "${USER}/ayni/connection.json";
            throw RuntimeError(
                std::format("Could not read '{}'{}", url, hint),
                std::string(_loggerCat)
            );
        }
        return std::string(data.begin(), data.end());
    }

    ayni::Release loadRelease(const std::string& url,
                              const std::optional<std::string>& token)
    {
        std::string releaseUrl = url;
        if (!url.ends_with("release.json")) {
            releaseUrl = ayni::releaseUrlFromPointer(download(url, token), url);
        }
        LINFO(std::format("Reading release '{}'", releaseUrl));
        ayni::Release release =
            ayni::parseRelease(download(releaseUrl, token), releaseUrl);
        if (!release.valueEncoding.has_value()) {
            throw RuntimeError(
                std::format("Release '{}' is not value-encoded", release.releaseId),
                std::string(_loggerCat)
            );
        }

        // libmpv sends the token with every request it makes for the playlist, so the
        // playlist must not point anywhere but the stream host, and neither may the MPD
        if (token.has_value()) {
            bool isOnStreamHost = ayni::isOnStreamHost(release.initializationUrl);
            for (const ayni::Segment& segment : release.segments) {
                isOnStreamHost = isOnStreamHost && ayni::isOnStreamHost(segment.url);
            }
            if (!release.mpdUrl.empty()) {
                isOnStreamHost = isOnStreamHost && ayni::isOnStreamHost(release.mpdUrl);
            }
            if (!isOnStreamHost) {
                throw RuntimeError(
                    std::format(
                        "Release '{}' has media outside the stream host",
                        release.releaseId
                    ),
                    std::string(_loggerCat)
                );
            }
        }

        // Historical releases, such as the maps of other bodies, leave the segment
        // lengths to their MPD
        const bool hasLengths = std::all_of(
            release.segments.begin(),
            release.segments.end(),
            [](const ayni::Segment& segment) { return segment.durationSeconds > 0.0; }
        );
        if (!hasLengths) {
            ayni::setSegmentDurations(release, download(release.mpdUrl, token));
        }
        return release;
    }

    // Writes the release's segments as an mpv playlist and returns the settings of the
    // video player that plays it
    Dictionary videoPlayerDictionary(const ayni::Release& release) {
        const std::filesystem::path folder = absPath("${TEMPORARY}/ayni");
        std::filesystem::create_directories(folder);
        // The nodes of a cluster can share the folder, so each writes its own file
        const std::filesystem::path playlist = folder / std::format(
            "{}-{}.edl", release.releaseId, global::windowDelegate->currentNode()
        );
        {
            std::ofstream file = std::ofstream(playlist, std::ios::binary);
            file << ayni::edlPlaylist(release);
            if (!file.good()) {
                throw RuntimeError(
                    std::format("Could not write '{}'", playlist.string()),
                    std::string(_loggerCat)
                );
            }
        }

        Dictionary dictionary;
        dictionary.setValue("Video", playlist.string());
        dictionary.setValue("PlaybackMode", std::string("MapToSimulationTime"));
        dictionary.setValue("StartTime", ayni::openspaceDateTime(release.startTime));
        dictionary.setValue("EndTime", ayni::openspaceDateTime(release.endTime));
        return dictionary;
    }
} // namespace
#include "aynistreamtileprovider_codegen.cpp"

namespace openspace {

Documentation AyniStreamTileProvider::Documentation() {
    return codegen::doc<Parameters>("ayni_tileprovider_stream");
}

AyniStreamTileProvider::AyniStreamTileProvider(const Dictionary& dictionary)
    : _release(
        loadRelease(codegen::bake<Parameters>(dictionary).url, streamAccess().token())
    )
    , _videoPlayer(videoPlayerDictionary(_release))
{
    ZoneScoped;

    ayni::StreamAccess& access = streamAccess();
    const std::optional<std::string> token = access.token();
    if (token.has_value()) {
        _videoPlayer.setMpvOption(
            "http-header-fields",
            ayni::authorizationHeader(*token)
        );
    }
    _tokenGeneration = access.generation();
    _tokenExpiresAt = access.expiresAt();
    addPropertySubOwner(_videoPlayer);
}

AyniStreamTileProvider::~AyniStreamTileProvider() = default;

const ayni::Release& AyniStreamTileProvider::release() const {
    return _release;
}

void AyniStreamTileProvider::internalInitialize() {
    ZoneScoped;

    // Dithering would move a decoded code to its neighbours; the codes are data
    _videoPlayer.setMpvOption("dither-depth", "no");
    _videoPlayer.initialize();

    _program = opengl::ProgramObject::Build(
        "AyniStreamColor",
        absPath("${MODULE_AYNI}/shaders/streamcolor_vs.glsl"),
        absPath("${MODULE_AYNI}/shaders/streamcolor_fs.glsl")
    );
    opengl::updateUniformLocations(*_program, _uniformCache);

    const std::array<glm::u8vec4, 256> lookup =
        ayni::paletteLookup(*_release.valueEncoding);
    _paletteTexture = std::make_unique<opengl::Texture>(
        opengl::Texture::FormatInit {
            .dimensions = glm::uvec3(256, 1, 1),
            .type = GL_TEXTURE_2D,
            .format = opengl::Texture::Format::RGBA,
            .dataType = GL_UNSIGNED_BYTE
        },
        opengl::Texture::SamplerInit {
            .filter = opengl::Texture::FilterMode::Nearest,
            .wrapping = opengl::Texture::WrappingMode::ClampToEdge
        },
        reinterpret_cast<const std::byte*>(lookup.data())
    );

    const glm::ivec2 size = _release.valueEncoding->dataRegionSize;
    _colorTexture = std::make_unique<opengl::Texture>(
        opengl::Texture::FormatInit {
            .dimensions = glm::uvec3(size, 1),
            .type = GL_TEXTURE_2D,
            .format = opengl::Texture::Format::RGBA,
            .dataType = GL_UNSIGNED_BYTE
        },
        // A regional map covers part of the globe; around it the layer is transparent
        opengl::Texture::SamplerInit {
            .filter = opengl::Texture::FilterMode::Linear,
            .wrapping = opengl::Texture::WrappingMode::ClampToBorder,
            .borderColor = glm::vec4(0.f)
        }
    );

    glCreateFramebuffers(1, &_fbo);
    glNamedFramebufferTexture(_fbo, GL_COLOR_ATTACHMENT0, *_colorTexture, 0);
    glCreateVertexArrays(1, &_vao);
}

void AyniStreamTileProvider::internalDeinitialize() {
    _videoPlayer.destroy();
    glDeleteVertexArrays(1, &_vao);
    _vao = 0;
    glDeleteFramebuffers(1, &_fbo);
    _fbo = 0;
    _colorTexture = nullptr;
    _paletteTexture = nullptr;
    _program = nullptr;
    _hasFrame = false;
}

void AyniStreamTileProvider::update() {
    ZoneScoped;

    // A token lives for ten minutes; libmpv sends the newest one with every request
    ayni::StreamAccess& access = streamAccess();
    if (access.hasConnection()) {
        access.update();
        if (access.generation() != _tokenGeneration) {
            // libmpv stops playing at the first segment that the stream host refused,
            // so a token that came after the old one expired needs a reload
            const bool hasLapsed = std::chrono::steady_clock::now() >= _tokenExpiresAt;
            _tokenGeneration = access.generation();
            _tokenExpiresAt = access.expiresAt();
            _videoPlayer.setMpvOption(
                "http-header-fields",
                ayni::authorizationHeader(*access.token())
            );
            if (hasLapsed) {
                LINFO("Reloading the stream with a new token");
                _videoPlayer.reload();
            }
        }
    }

    _videoPlayer.update();
    if (_program && _program->isDirty()) [[unlikely]] {
        _program->rebuildFromFile();
        opengl::updateUniformLocations(*_program, _uniformCache);
    }
    colorFrame();
}

void AyniStreamTileProvider::colorFrame() {
    ZoneScoped;

    const std::unique_ptr<opengl::Texture>& frame = _videoPlayer.frameTexture();
    if (!_videoPlayer.isInitialized() || !frame || !_program) {
        return;
    }
    // Until mpv has reported the video's size, the frame texture has a default size
    const glm::ivec2 frameSize = glm::ivec2(frame->dimensions());
    if (frameSize != _release.frameSize) {
        return;
    }

    // The frame texture's first row is the bottom of the picture, while the data
    // region is measured from the top
    const ayni::ValueEncoding& enc = *_release.valueEncoding;
    const glm::ivec2 offset = glm::ivec2(
        enc.dataRegionOffset.x,
        frameSize.y - enc.dataRegionOffset.y - enc.dataRegionSize.y
    );

    const GLint defaultFbo = opengl::FramebufferObject::getActiveObject();
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
    glViewport(0, 0, enc.dataRegionSize.x, enc.dataRegionSize.y);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    _program->activate();
    opengl::TextureUnit frameUnit;
    frameUnit.bind(*frame);
    _program->setUniform(_uniformCache.videoFrame, frameUnit);
    opengl::TextureUnit paletteUnit;
    paletteUnit.bind(*_paletteTexture);
    _program->setUniform(_uniformCache.palette, paletteUnit);
    _program->setUniform(_uniformCache.dataRegionOffset, offset);

    glBindVertexArray(_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    _program->deactivate();

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFbo);
    global::renderEngine->openglStateCache().resetViewportState();
    global::renderEngine->openglStateCache().resetBlendState();
    global::renderEngine->openglStateCache().resetDepthState();
    _hasFrame = true;
}

Tile AyniStreamTileProvider::tile(const TileIndex&) {
    if (!_hasFrame) {
        return Tile();
    }
    return Tile { _colorTexture.get(), std::nullopt, Tile::Status::OK };
}

Tile::Status AyniStreamTileProvider::tileStatus(const TileIndex& tileIndex) {
    if (tileIndex.level > maxLevel()) {
        return Tile::Status::OutOfRange;
    }
    return _hasFrame ? Tile::Status::OK : Tile::Status::Unavailable;
}

TileDepthTransform AyniStreamTileProvider::depthTransform() {
    return { 0.f, 1.f };
}

void AyniStreamTileProvider::reset() {
    _videoPlayer.reload();
}

ChunkTile AyniStreamTileProvider::chunkTile(TileIndex tileIndex, int parents,
                                            int maxParents)
{
    constexpr auto ascendToParent = [](TileIndex& ti, TileUvTransform&) {
        ti.level--;
    };

    // The equirectangular map is one texture; each chunk shows its part of it. The
    // texture's first row is the south edge of the map
    const ayni::ValueEncoding& enc = *_release.valueEncoding;
    const double nTilesX = std::pow(2.0, tileIndex.level);
    const double nTilesY = std::pow(2.0, tileIndex.level - 1);
    const double tileWidth = 360.0 / nTilesX;
    const double tileHeight = 180.0 / nTilesY;
    const double west = -180.0 + tileWidth * static_cast<double>(tileIndex.x);
    // The tiles on the y-axis are counted from the north
    const double south = 90.0 - tileHeight * static_cast<double>(tileIndex.y + 1);
    const double mapWidth = enc.lonRight - enc.lonLeft;
    const double mapHeight = enc.latTop - enc.latBottom;

    TileUvTransform uvTransform = {
        glm::vec2(
            static_cast<float>((west - enc.lonLeft) / mapWidth),
            static_cast<float>((south - enc.latBottom) / mapHeight)
        ),
        glm::vec2(
            static_cast<float>(tileWidth / mapWidth),
            static_cast<float>(tileHeight / mapHeight)
        )
    };
    return traverseTree(tileIndex, parents, maxParents, ascendToParent, uvTransform);
}

int AyniStreamTileProvider::minLevel() {
    return 1;
}

int AyniStreamTileProvider::maxLevel() {
    // One texture covers every level, so the layer never runs out of detail
    return 1337;
}

float AyniStreamTileProvider::noDataValueAsFloat() {
    return std::numeric_limits<float>::min();
}

} // namespace openspace
