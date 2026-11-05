{
  "assets": [
    "base_blank",
    "base_keybindings",
    "events/toggle_sun",
    "modules/ayni/streams",
    "scene/solarsystem/sun/sun",
    "scene/solarsystem/planets/planets",
    "scene/solarsystem/planets/default_layers",
    "util/joysticks/openxr-controllers"
  ],
  "camera": {
    "altitude": 17000000.0,
    "anchor": "Earth",
    "latitude": 41.3874,
    "longitude": 2.1686,
    "type": "goToGeo"
  },
  "delta_times": [
    1.0,
    60.0,
    3600.0,
    86400.0,
    604800.0,
    2592000.0,
    31536000.0
  ],
  "mark_nodes": [
    "Sun",
    "Mercury",
    "Venus",
    "Earth",
    "Moon",
    "Mars",
    "Jupiter",
    "Saturn",
    "Uranus",
    "Neptune"
  ],
  "meta": {
    "author": "OpenSpace in VR",
    "description": "Headset profile: the Sun, the planets with their major moons and labels, navigated with the OpenXR controllers. No roll, calmer camera, no idle motion, and adaptive stereo depth tuned so the focused surface appears about 3 m away in a headset instead of the 40 m dome default. Earth's day side uses date-independent imagery (ESRI World Imagery over Blue Marble) so it stays textured when time runs fast or into the future.",
    "license": "MIT License",
    "name": "VR",
    "url": "https://github.com/jphpme/OpenSpace",
    "version": "1.0"
  },
  "properties": [
    {
      "name": "{solarsystem_labels}.Renderable.Enabled",
      "type": "setPropertyValue",
      "value": "true"
    },
    {
      "name": "Scene.Earth.Renderable.Layers.ColorLayers.ESRI_VIIRS_Combo.Enabled",
      "type": "setPropertyValueSingle",
      "value": "false"
    },
    {
      "name": "Scene.Earth.Renderable.Layers.ColorLayers.Blue_Marble.Enabled",
      "type": "setPropertyValueSingle",
      "value": "true"
    },
    {
      "name": "Scene.Earth.Renderable.Layers.ColorLayers.ESRI_World_Imagery.Enabled",
      "type": "setPropertyValueSingle",
      "value": "true"
    },
    {
      "name": "NavigationHandler.OrbitalNavigator.DisableRoll",
      "type": "setPropertyValueSingle",
      "value": "true"
    },
    {
      "name": "NavigationHandler.OrbitalNavigator.UseAdaptiveStereoscopicDepth",
      "type": "setPropertyValueSingle",
      "value": "true"
    },
    {
      "name": "NavigationHandler.OrbitalNavigator.StereoscopicDepthOfFocusSurface",
      "type": "setPropertyValueSingle",
      "value": "3.0"
    },
    {
      "name": "NavigationHandler.OrbitalNavigator.StereoInterpolationTime",
      "type": "setPropertyValueSingle",
      "value": "2.0"
    },
    {
      "name": "NavigationHandler.OrbitalNavigator.Input.MouseSensitivity",
      "type": "setPropertyValueSingle",
      "value": "8.0"
    },
    {
      "name": "NavigationHandler.OrbitalNavigator.Input.JoystickSensitivity",
      "type": "setPropertyValueSingle",
      "value": "5.0"
    },
    {
      "name": "NavigationHandler.OrbitalNavigator.IdleMotion.Apply",
      "type": "setPropertyValueSingle",
      "value": "false"
    }
  ],
  "time": {
    "is_paused": false,
    "type": "relative",
    "value": "-1d"
  },
  "version": {
    "major": 1,
    "minor": 5
  }
}
