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

#ifndef __OPENSPACE_MODULE_AYNI___AYNISTREAMTILEPROVIDER___H__
#define __OPENSPACE_MODULE_AYNI___AYNISTREAMTILEPROVIDER___H__

#include <modules/globebrowsing/src/tileprovider/tileprovider.h>

#include <modules/ayni/src/aynirelease.h>
#include <modules/video/include/videoplayer.h>
#include <openspace/opengl/uniformcache.h>
#include <memory>
#include <optional>
#include <string>

namespace openspace {

namespace opengl {
    class ProgramObject;
    class Texture;
} // namespace opengl

/**
 * Plays a value-encoded AYNI stream release on a globe. The release's segments are played
 * through libmpv, mapped to the simulation time the release covers, and every decoded
 * frame is coloured with the release's palette: no data is transparent and the
 * calibration strip is cropped off.
 */
class AyniStreamTileProvider : public TileProvider {
public:
    explicit AyniStreamTileProvider(const Dictionary& dictionary);
    ~AyniStreamTileProvider() override;

    void update() override;
    void reset() override;
    int minLevel() override;
    int maxLevel() override;
    float noDataValueAsFloat() override;
    ChunkTile chunkTile(TileIndex tileIndex, int parents, int maxParents = 1337) override;
    Tile tile(const TileIndex& tileIndex) override;
    Tile::Status tileStatus(const TileIndex& tileIndex) override;
    TileDepthTransform depthTransform() override;

    static openspace::Documentation Documentation();

private:
    void internalInitialize() override;
    void internalDeinitialize() override;

    /// Colours the current video frame into `_colorTexture`
    void colorFrame();

    ayni::Release _release;
    VideoPlayer _videoPlayer;
    /// The `ayni::StreamAccess::generation` of the token that libmpv sends
    int _tokenGeneration = 0;

    std::unique_ptr<opengl::ProgramObject> _program;
    UniformCache(videoFrame, palette, dataRegionOffset) _uniformCache;
    std::unique_ptr<opengl::Texture> _paletteTexture;
    std::unique_ptr<opengl::Texture> _colorTexture;
    GLuint _fbo = 0;
    GLuint _vao = 0;
    bool _hasFrame = false;
};

} // namespace openspace

#endif // __OPENSPACE_MODULE_AYNI___AYNISTREAMTILEPROVIDER___H__
