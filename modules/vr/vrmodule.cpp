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

#include <openspace/camera/camera.h>
#include <openspace/camera/camerapose.h>
#include <openspace/engine/globals.h>
#include <openspace/engine/globalscallbacks.h>
#include <openspace/engine/openspaceengine.h>
#include <openspace/engine/windowdelegate.h>
#include <openspace/filesystem/filesystem.h>
#include <openspace/misc/profiling.h>
#include <openspace/navigation/navigationhandler.h>
#include <openspace/navigation/orbitalnavigator/orbitalnavigator.h>
#include <openspace/opengl/openglstatecache.h>
#include <openspace/opengl/programobject.h>
#include <openspace/query/query.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/scene/scene.h>
#include <openspace/scene/scenegraphnode.h>
#include <glm/gtx/quaternion.hpp>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
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

    constexpr BoolPropertyInfo FreeFlightInfo = {
        .identifier = "FreeFlight",
        .name = "Free flight",
        .description = "If enabled, grabbing with a controller moves the user through the "
            "scene and twisting both hands turns the scene around the user. If disabled, "
            "grabbing turns the focus and spreading both hands zooms towards it. The VR "
            "navigation actions set this together with the controller bindings.",
        .visibility = Visibility::User,
        .defaults = {
            .value = false
        }
    };

    constexpr BoolPropertyInfo GrabEnabledInfo = {
        .identifier = "GrabEnabled",
        .name = "Grab enabled",
        .description = "If enabled, holding the trigger of a VR controller grabs the scene "
            "so that it follows the hand.",
        .visibility = Visibility::User,
        .defaults = {
            .value = true
        }
    };

    constexpr FloatPropertyInfo GrabTranslationScaleInfo = {
        .identifier = "GrabTranslationScale",
        .name = "Grab translation scale",
        .description = "How far grabbing in free flight moves the user per meter of hand "
            "movement, as a fraction of the distance to the closest surface.",
        .visibility = Visibility::User,
        .defaults = {
            .value = 1.f,
            .minMax = { 0.01f, 10.f }
        }
    };

    // A trigger that is pressed further than this grabs the scene
    constexpr float TriggerThreshold = 0.5f;
    // Hands closer together than this, in meters, do not twist or zoom
    constexpr float MinimumHandDistance = 0.01f;

    // The direction, in camera space, that the pointing ray of a controller points in
    glm::dvec3 aimDirection(const glm::mat4& pose) {
        return glm::normalize(glm::dvec3(glm::mat3(pose) * glm::vec3(0.f, 0.f, -1.f)));
    }

    glm::dvec3 handPosition(const glm::mat4& pose) {
        return glm::dvec3(pose[3]);
    }

    // The nearest point where a ray from the origin along the unit vector `direction`
    // hits the sphere around `center`
    std::optional<glm::dvec3> intersectSphere(const glm::dvec3& direction,
                                              const glm::dvec3& center, double radius)
    {
        const double b = glm::dot(direction, center);
        const double c = glm::dot(center, center) - radius * radius;
        const double discriminant = b * b - c;
        if (discriminant < 0.0) {
            return std::nullopt;
        }
        const double root = std::sqrt(discriminant);
        // From inside the sphere the near intersection is behind the origin
        const double t = b - root >= 0.0 ? b - root : b + root;
        if (t < 0.0) {
            return std::nullopt;
        }
        return direction * t;
    }

    glm::dquat rotationBetween(const glm::dvec3& from, const glm::dvec3& to) {
        return glm::rotation(glm::normalize(from), glm::normalize(to));
    }

    // Moves the camera around `center` so that, seen from the camera, the scene turns
    // by `turn` (in camera space) around `center`. The distance to `center` is
    // multiplied by `distanceScale`
    void orbitCamera(Camera& camera, const glm::dvec3& center, const glm::dquat& turn,
                     double distanceScale)
    {
        const glm::dquat rotation = camera.rotationQuaternion();
        const glm::dquat worldTurn = rotation * glm::inverse(turn) * glm::inverse(rotation);
        const glm::dvec3 offset = worldTurn * (camera.position() - center);
        camera.setPose({
            .position = center + offset * distanceScale,
            .rotation = worldTurn * rotation
        });
    }

    // The distance from `position` to the closest surface (interaction sphere) of any
    // scene graph node that does not contain `position`
    double closestSurfaceDistance(const glm::dvec3& position) {
        double distance = std::numeric_limits<double>::max();
        for (const SceneGraphNode* node : sceneGraph()->allSceneGraphNodes()) {
            const double radius = node->interactionSphere();
            if (radius <= 0.0) {
                continue;
            }
            const double d = glm::length(node->worldPosition() - position) - radius;
            if (d > 0.0) {
                distance = std::min(distance, d);
            }
        }
        return distance == std::numeric_limits<double>::max() ? 1.0 : distance;
    }

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
    , _freeFlight(FreeFlightInfo)
    , _grabEnabled(GrabEnabledInfo)
    , _grabTranslationScale(GrabTranslationScaleInfo)
{
    addProperty(_showControllers);
    addProperty(_showRays);
    addProperty(_rayLength);
    _controllerColor.setViewOption(Property::ViewOptions::Color);
    addProperty(_controllerColor);
    _rayColor.setViewOption(Property::ViewOptions::Color);
    addProperty(_rayColor);
    addProperty(_freeFlight);
    addProperty(_grabEnabled);
    addProperty(_grabTranslationScale);
}

void VRModule::internalInitialize(const Dictionary&) {
    global::callback::preSync->push_back([this]() { updateGrab(); });

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

void VRModule::updateGrab() {
    ZoneScoped;

    std::array<std::optional<glm::mat4>, 2> poses;
    for (int hand = 0; hand < 2; hand++) {
        const std::optional<glm::mat4> pose =
            global::windowDelegate->vrControllerAimPose(hand);
        const bool isGrabbing = _grabEnabled && pose.has_value() &&
            global::windowDelegate->vrControllerTrigger(hand) > TriggerThreshold;
        poses[hand] = isGrabbing ? pose : std::optional<glm::mat4>();
    }

    // Camera paths and session recordings control the camera themselves
    const bool canMoveCamera =
        global::openSpaceEngine->currentMode() == OpenSpaceEngine::Mode::UserControl &&
        global::navigationHandler->camera();
    if (canMoveCamera) {
        const bool isTwoHanded = poses[0].has_value() && poses[1].has_value();
        const bool wasTwoHanded =
            _previousGrabPoses[0].has_value() && _previousGrabPoses[1].has_value();
        if (isTwoHanded && wasTwoHanded) {
            const glm::vec3 previousHandVector = glm::vec3(
                handPosition(*_previousGrabPoses[1]) - handPosition(*_previousGrabPoses[0])
            );
            const glm::vec3 handVector =
                glm::vec3(handPosition(*poses[1]) - handPosition(*poses[0]));
            grabWithTwoHands(previousHandVector, handVector);
        }
        else if (!isTwoHanded) {
            // When one of two grabbing hands lets go, the other one keeps grabbing
            for (int hand = 0; hand < 2; hand++) {
                if (poses[hand].has_value() && _previousGrabPoses[hand].has_value()) {
                    grabWithOneHand(*_previousGrabPoses[hand], *poses[hand]);
                }
            }
        }
    }
    _previousGrabPoses = poses;
}

void VRModule::grabWithOneHand(const glm::mat4& previousPose, const glm::mat4& pose) {
    Camera* camera = global::navigationHandler->camera();
    const glm::dvec3 position = camera->position();
    const glm::dquat rotation = camera->rotationQuaternion();

    if (_freeFlight) {
        // The scene moves with the hand, so the camera moves the opposite way
        const glm::dvec3 handMotion = handPosition(pose) - handPosition(previousPose);
        const double scale = closestSurfaceDistance(position) * _grabTranslationScale;
        camera->setPose({
            .position = position - rotation * handMotion * scale,
            .rotation = rotation
        });
        return;
    }

    const SceneGraphNode* anchor =
        global::navigationHandler->orbitalNavigator().anchorNode();
    if (!anchor) {
        return;
    }
    const glm::dvec3 anchorPosition = anchor->worldPosition();
    const glm::dvec3 center = glm::inverse(rotation) * (anchorPosition - position);
    const double radius = anchor->interactionSphere();

    // The point of the focus that was under the ray is turned to be under the ray again.
    // If the ray misses the focus, the focus turns like the sky around the user
    const glm::dvec3 from = aimDirection(previousPose);
    const glm::dvec3 to = aimDirection(pose);
    const std::optional<glm::dvec3> fromHit = intersectSphere(from, center, radius);
    const std::optional<glm::dvec3> toHit = intersectSphere(to, center, radius);
    const glm::dquat turn = fromHit.has_value() && toHit.has_value() ?
        rotationBetween(*fromHit - center, *toHit - center) :
        rotationBetween(from, to);
    orbitCamera(*camera, anchorPosition, turn, 1.0);
}

void VRModule::grabWithTwoHands(const glm::vec3& previousHandVector,
                                const glm::vec3& handVector)
{
    if (glm::length(previousHandVector) < MinimumHandDistance ||
        glm::length(handVector) < MinimumHandDistance)
    {
        return;
    }

    Camera* camera = global::navigationHandler->camera();
    const glm::dquat turn =
        rotationBetween(glm::dvec3(previousHandVector), glm::dvec3(handVector));

    if (_freeFlight) {
        // The scene turns around the user
        camera->setPose({
            .position = camera->position(),
            .rotation = camera->rotationQuaternion() * glm::inverse(turn)
        });
        return;
    }

    const SceneGraphNode* anchor =
        global::navigationHandler->orbitalNavigator().anchorNode();
    if (!anchor) {
        return;
    }
    // Spreading the hands apart brings the focus closer. The height above the surface
    // is scaled so that the camera never reaches the surface
    const glm::dvec3 anchorPosition = anchor->worldPosition();
    const double radius = anchor->interactionSphere();
    const double distance = glm::length(camera->position() - anchorPosition);
    if (distance <= 0.0) {
        return;
    }
    const double spread =
        static_cast<double>(glm::length(handVector) / glm::length(previousHandVector));
    const double height = std::max(distance - radius, 0.0);
    const double newDistance = radius + height / spread;
    orbitCamera(*camera, anchorPosition, turn, newDistance / distance);
}

} // namespace openspace
