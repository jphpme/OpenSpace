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

#include <modules/vr/vrmodule.h>

#include <openspace/engine/globals.h>
#include <openspace/engine/globalscallbacks.h>
#include <openspace/engine/windowdelegate.h>
#include <openspace/filesystem/filesystem.h>
#include <openspace/misc/profiling.h>
#include <openspace/opengl/openglstatecache.h>
#include <openspace/opengl/programobject.h>
#include <openspace/rendering/renderengine.h>
#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace {
    using namespace openspace;

    struct Vertex {
        glm::vec3 position;
        glm::vec3 normal;
    };

    // Number of vertices in the unit cube that every controller part is drawn with
    constexpr int CubeVertexCount = 36;

    // A unit cube centered on the origin, as two triangles per face with face normals
    std::vector<Vertex> cubeVertices() {
        std::vector<Vertex> vertices;
        vertices.reserve(CubeVertexCount);
        for (int axis = 0; axis < 3; axis++) {
            for (const float side : { -1.f, 1.f }) {
                glm::vec3 normal = glm::vec3(0.f);
                normal[axis] = side;
                glm::vec3 u = glm::vec3(0.f);
                u[(axis + 1) % 3] = 0.5f;
                glm::vec3 v = glm::vec3(0.f);
                v[(axis + 2) % 3] = 0.5f;

                const glm::vec3 center = 0.5f * normal;
                const std::array<glm::vec3, 4> corners = {
                    center - u - v,
                    center + u - v,
                    center + u + v,
                    center - u + v
                };
                for (const int i : { 0, 1, 2, 0, 2, 3 }) {
                    vertices.push_back({ corners[i], normal });
                }
            }
        }
        return vertices;
    }

    constexpr BoolPropertyInfo ShowControllersInfo = {
        .identifier = "ShowControllers",
        .name = "Show controllers",
        .description = "If enabled, a simple model of each tracked hand controller of a "
            "VR headset is drawn at its position.",
        .visibility = Visibility::User,
        .defaults = {
            .value = true
        }
    };

    constexpr BoolPropertyInfo ShowRaysInfo = {
        .identifier = "ShowRays",
        .name = "Show rays",
        .description = "If enabled, a pointing ray is drawn from each tracked hand "
            "controller of a VR headset.",
        .visibility = Visibility::User,
        .defaults = {
            .value = true
        }
    };

    constexpr FloatPropertyInfo RayLengthInfo = {
        .identifier = "RayLength",
        .name = "Ray length",
        .description = "The length of the pointing rays, in meters.",
        .visibility = Visibility::User,
        .defaults = {
            .value = 3.f,
            .minMax = { 0.1f, 20.f }
        }
    };

    constexpr Vec3PropertyInfo ControllerColorInfo = {
        .identifier = "ControllerColor",
        .name = "Controller color",
        .description = "The color of the controller models.",
        .visibility = Visibility::User,
        .defaults = {
            .value = glm::vec3(0.75f, 0.75f, 0.8f),
            .minMax = { glm::vec3(0.f), glm::vec3(1.f) }
        }
    };

    constexpr Vec3PropertyInfo RayColorInfo = {
        .identifier = "RayColor",
        .name = "Ray color",
        .description = "The color of the pointing rays.",
        .visibility = Visibility::User,
        .defaults = {
            .value = glm::vec3(0.3f, 0.8f, 1.f),
            .minMax = { glm::vec3(0.f), glm::vec3(1.f) }
        }
    };

    // Size of the controller model in meters and how far its center sits behind the
    // aim pose, which points forward along -z from the front of the controller
    constexpr glm::vec3 ControllerSize = glm::vec3(0.03f, 0.025f, 0.11f);
    constexpr float ControllerOffset = 0.05f;
    // Thickness of the pointing ray in meters
    constexpr float RayWidth = 0.004f;
    constexpr float RayOpacity = 0.6f;
} // namespace

namespace openspace {

VRModule::VRModule()
    : OpenSpaceModule(Name)
    , _showControllers(ShowControllersInfo)
    , _showRays(ShowRaysInfo)
    , _rayLength(RayLengthInfo)
    , _controllerColor(ControllerColorInfo)
    , _rayColor(RayColorInfo)
{
    addProperty(_showControllers);
    addProperty(_showRays);
    addProperty(_rayLength);
    _controllerColor.setViewOption(Property::ViewOptions::Color);
    addProperty(_controllerColor);
    _rayColor.setViewOption(Property::ViewOptions::Color);
    addProperty(_rayColor);
}

void VRModule::internalInitialize(const Dictionary&) {
    global::callback::render->push_back(
        [this](const glm::mat4&, const glm::mat4& viewMatrix,
               const glm::mat4& projectionMatrix)
        {
            renderControllers(viewMatrix, projectionMatrix);
        }
    );
}

void VRModule::internalInitializeGL() {
    _program = opengl::ProgramObject::Build(
        "VRControllerProgram",
        absPath("${MODULE_VR}/shaders/controller_vs.glsl"),
        absPath("${MODULE_VR}/shaders/controller_fs.glsl")
    );
    opengl::updateUniformLocations(*_program, _uniformCache);

    const std::vector<Vertex> vertices = cubeVertices();
    glCreateBuffers(1, &_vbo);
    glNamedBufferStorage(
        _vbo,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
        vertices.data(),
        GL_NONE_BIT
    );

    glCreateVertexArrays(1, &_vao);
    glVertexArrayVertexBuffer(_vao, 0, _vbo, 0, sizeof(Vertex));

    glEnableVertexArrayAttrib(_vao, 0);
    glVertexArrayAttribFormat(_vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glVertexArrayAttribBinding(_vao, 0, 0);

    glEnableVertexArrayAttrib(_vao, 1);
    glVertexArrayAttribFormat(_vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
    glVertexArrayAttribBinding(_vao, 1, 0);
}

void VRModule::internalDeinitializeGL() {
    glDeleteVertexArrays(1, &_vao);
    _vao = 0;
    glDeleteBuffers(1, &_vbo);
    _vbo = 0;
    _program = nullptr;
}

void VRModule::renderControllers(const glm::mat4& viewMatrix,
                                 const glm::mat4& projectionMatrix)
{
    ZoneScoped;

    if (!_program || (!_showControllers && !_showRays)) {
        return;
    }

    struct Part {
        glm::mat4 model;
        glm::vec4 color;
    };
    // Opaque controller models first, then the transparent rays
    std::vector<Part> models;
    std::vector<Part> rays;
    for (int hand = 0; hand < 2; hand++) {
        const std::optional<glm::mat4> pose =
            global::windowDelegate->vrControllerAimPose(hand);
        if (!pose.has_value()) {
            continue;
        }

        if (_showControllers) {
            const glm::mat4 model = *pose *
                glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, ControllerOffset)) *
                glm::scale(glm::mat4(1.f), ControllerSize);
            models.push_back({ model, glm::vec4(_controllerColor.value(), 1.f) });
        }
        if (_showRays) {
            const float length = _rayLength;
            const glm::mat4 model = *pose *
                glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, -0.5f * length)) *
                glm::scale(glm::mat4(1.f), glm::vec3(RayWidth, RayWidth, length));
            rays.push_back({ model, glm::vec4(_rayColor.value(), RayOpacity) });
        }
    }
    if (models.empty() && rays.empty()) {
        return;
    }

    // The scene's depth is not available here, so the controllers are drawn on top of
    // everything and the depth buffer only sorts the controller parts among themselves
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    _program->activate();
    glBindVertexArray(_vao);
    for (const std::vector<Part>* parts : { &models, &rays }) {
        for (const Part& part : *parts) {
            const glm::mat4 modelView = viewMatrix * part.model;
            _program->setUniform(
                _uniformCache.modelViewProjection,
                projectionMatrix * modelView
            );
            _program->setUniform(_uniformCache.modelView, modelView);
            _program->setUniform(_uniformCache.color, part.color);
            glDrawArrays(GL_TRIANGLES, 0, CubeVertexCount);
        }
    }
    glBindVertexArray(0);
    _program->deactivate();

    global::renderEngine->openglStateCache().resetBlendState();
    global::renderEngine->openglStateCache().resetDepthState();
}

} // namespace openspace
