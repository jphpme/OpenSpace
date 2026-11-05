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

#ifndef __OPENSPACE_MODULE_VR___VRMODULE___H__
#define __OPENSPACE_MODULE_VR___VRMODULE___H__

#include <openspace/util/openspacemodule.h>

#include <openspace/opengl/gl.h>
#include <openspace/opengl/uniformcache.h>
#include <openspace/properties/scalar/boolproperty.h>
#include <openspace/properties/scalar/floatproperty.h>
#include <openspace/properties/vector/vec3property.h>
#include <memory>

namespace openspace {

namespace opengl { class ProgramObject; }

/**
 * Draws the hand controllers of a VR headset, and a pointing ray from each of them, on
 * top of the rendered scene. The controller poses come from the WindowDelegate.
 */
class VRModule : public OpenSpaceModule {
public:
    constexpr static const char* Name = "VR";

    VRModule();
    ~VRModule() override = default;

protected:
    void internalInitialize(const Dictionary&) override;
    void internalInitializeGL() override;
    void internalDeinitializeGL() override;

private:
    void renderControllers(const glm::mat4& viewMatrix,
        const glm::mat4& projectionMatrix);

    BoolProperty _showControllers;
    BoolProperty _showRays;
    FloatProperty _rayLength;
    Vec3Property _controllerColor;
    Vec3Property _rayColor;

    std::unique_ptr<opengl::ProgramObject> _program;
    UniformCache(modelViewProjection, modelView, color) _uniformCache;
    GLuint _vao = 0;
    GLuint _vbo = 0;
};

} // namespace openspace

#endif // __OPENSPACE_MODULE_VR___VRMODULE___H__
