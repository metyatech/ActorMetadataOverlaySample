"""Build the public overview map when explicitly run inside Unreal Editor."""

import json
import os
import time

import unreal


MAP_PATH = "/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview"
FIXTURE_MODULE = "ActorMetadataOverlayDemoFixtures"
ACTOR_CLASSES = {
    "AActorMetadataOverlayDemoActor": "/Script/ActorMetadataOverlayDemoFixtures.ActorMetadataOverlayDemoActor",
    "AActorMetadataOverlayDemoZone": "/Script/ActorMetadataOverlayDemoFixtures.ActorMetadataOverlayDemoZone",
}
ENGINE_BASIC_SHAPES_PREFIX = "/Engine/BasicShapes/"
UNREAL_PYTHON_CLASS_NAMES = {
    "AStaticMeshActor": "StaticMeshActor",
    "ADirectionalLight": "DirectionalLight",
    "ASkyLight": "SkyLight",
    "ASkyAtmosphere": "SkyAtmosphere",
}
VISUAL_ENVIRONMENT_KEYS = (
    "floor",
    "directionalLight",
    "skyLight",
    "skyAtmosphere",
)


def project_file(relative_path):
    return os.path.join(unreal.Paths.project_dir(), relative_path.replace("/", os.sep))


def read_spec():
    with open(project_file("Demo/demo-spec.json"), "r", encoding="utf-8") as handle:
        return json.load(handle)


def get_required_generated_actor_names(spec):
    actor_entries = spec.get("actors")
    if not isinstance(actor_entries, list):
        raise RuntimeError("demo-spec.json key 'actors' must be a list of seven fixture entries")
    if len(actor_entries) != 7:
        raise RuntimeError("demo-spec.json key 'actors' must contain exactly 7 fixture entries (found {})".format(
            len(actor_entries)))

    names = []
    for index, entry in enumerate(actor_entries):
        if not isinstance(entry, dict):
            raise RuntimeError("demo-spec.json actors[{}] must be an object with actorName".format(index))
        actor_name = entry.get("actorName")
        if not isinstance(actor_name, str) or not actor_name.strip():
            raise RuntimeError("demo-spec.json actors[{}].actorName must be non-empty".format(index))
        names.append(actor_name)

    region_spec = spec.get("editorRegion")
    if not isinstance(region_spec, dict):
        raise RuntimeError("demo-spec.json key 'editorRegion' must be an object with actorName")
    region_name = region_spec.get("actorName")
    if not isinstance(region_name, str) or not region_name.strip():
        raise RuntimeError("demo-spec.json editorRegion.actorName must be non-empty")
    if region_name != "AMO_DemoRegion":
        raise RuntimeError("demo-spec.json editorRegion.actorName must be AMO_DemoRegion (found {})".format(
            region_name))
    names.append(region_name)

    visual_environment = spec.get("visualEnvironment")
    if not isinstance(visual_environment, dict):
        raise RuntimeError("demo-spec.json key 'visualEnvironment' must be an object")
    for key in VISUAL_ENVIRONMENT_KEYS:
        if key not in visual_environment or not isinstance(visual_environment[key], dict):
            raise RuntimeError("demo-spec.json visualEnvironment.{} must be present as an object".format(key))
        actor_name = visual_environment[key].get("actorName")
        if not isinstance(actor_name, str) or not actor_name.strip():
            raise RuntimeError("demo-spec.json visualEnvironment.{}.actorName must be non-empty".format(key))
        if not actor_name.startswith("AMO_Environment_"):
            raise RuntimeError("demo-spec.json visualEnvironment.{}.actorName must start with AMO_Environment_ (found {})".format(
                key, actor_name))
        names.append(actor_name)

    duplicate_names = sorted({name for name in names if names.count(name) > 1})
    if duplicate_names:
        raise RuntimeError("demo-spec.json generated actor names must be unique; duplicates: {}".format(
            ", ".join(duplicate_names)))
    if len(names) != 12:
        raise RuntimeError("demo-spec.json generated actor wait set must contain exactly 12 names (found {})".format(
            len(names)))
    return names


def set_property(actor, name, value):
    try:
        actor.set_editor_property(name, value)
    except Exception as error:
        raise RuntimeError("Could not set {} on {}: {}".format(name, actor.get_name(), error))


def resolve_unreal_class(class_name):
    python_class_name = UNREAL_PYTHON_CLASS_NAMES.get(class_name, class_name)
    actor_class = getattr(unreal, python_class_name, None)
    if actor_class is None:
        raise RuntimeError("The installed engine does not expose Unreal class {} (Python API name {})".format(
            class_name, python_class_name))
    return actor_class


def load_basic_shape_mesh(mesh_path):
    if not mesh_path.startswith(ENGINE_BASIC_SHAPES_PREFIX):
        raise RuntimeError("Visual mesh must be under /Engine/BasicShapes/: {}".format(mesh_path))
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if mesh is None or not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError("Could not load Engine Basic Shape mesh {}".format(mesh_path))
    return mesh


def get_exact_component(actor, component_class, description):
    components = actor.get_components_by_class(component_class)
    if len(components) != 1:
        raise RuntimeError("{} must have exactly one {} component, found {}".format(
            description, component_class.__name__, len(components)))
    return components[0]


def assign_basic_shape_mesh(actor, mesh_path, description):
    component = get_exact_component(actor, unreal.StaticMeshComponent, description)
    mesh = load_basic_shape_mesh(mesh_path)
    component.set_static_mesh(mesh)
    assigned_mesh = component.get_editor_property("static_mesh")
    if assigned_mesh is None or assigned_mesh.get_path_name() != mesh_path:
        raise RuntimeError("Could not assign Engine Basic Shape mesh {} to {}".format(mesh_path, description))
    return component


def ensure_data_layer_asset(asset_tools, name):
    package_path = "/Game/ActorMetadataOverlayDemo/DataLayers"
    asset_path = package_path + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return unreal.EditorAssetLibrary.load_asset(asset_path)

    factory_type = getattr(unreal, "DataLayerFactory", None)
    data_layer_type = getattr(unreal, "DataLayerAsset", None)
    if factory_type is None or data_layer_type is None:
        raise RuntimeError("The installed engine does not expose DataLayerFactory/DataLayerAsset to Python")
    asset = asset_tools.create_asset(name, package_path, data_layer_type, factory_type())
    if asset is None:
        raise RuntimeError("Could not create Data Layer asset {}".format(asset_path))
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    return asset


def ensure_data_layer_instances(data_layer_assets):
    subsystem_type = getattr(unreal, "DataLayerEditorSubsystem", None)
    params_type = getattr(unreal, "DataLayerCreationParameters", None)
    if subsystem_type is None or params_type is None:
        raise RuntimeError("The installed engine does not expose the Data Layer editor API")

    subsystem = unreal.get_editor_subsystem(subsystem_type)
    instances = {}
    for name, asset in data_layer_assets.items():
        instance = subsystem.get_data_layer_instance(asset)
        if instance is None:
            parameters = params_type()
            parameters.set_editor_property("data_layer_asset", asset)
            instance = subsystem.create_data_layer_instance(parameters)
        if instance is None:
            raise RuntimeError("Could not create Data Layer instance {}".format(name))
        instances[name] = instance
    return instances


def get_current_world(level_subsystem):
    current_level = level_subsystem.get_current_level()
    if current_level is None:
        raise RuntimeError("LevelEditorSubsystem returned no current level")
    world = current_level.get_outer()
    if world is None:
        raise RuntimeError("The current level has no owning world")
    return world


def clear_generated_actors(actor_subsystem):
    destroyed_count = 0
    for index, actor in enumerate(list(actor_subsystem.get_all_level_actors())):
        actor_name = actor.get_name()
        if actor_name.startswith("AMO_Environment_") or actor_name.startswith("AMO_") or actor.get_actor_label().startswith("Loot Crate"):
            temporary_name = "__AMO_DELETE_{}_{}".format(actor_name, index)
            actor.rename(temporary_name)
            if not actor_subsystem.destroy_actor(actor):
                raise RuntimeError("Could not remove generated actor {}".format(actor_name))
            destroyed_count += 1
    if destroyed_count:
        # EditorActorSubsystem marks actors for deletion; force the documented
        # UObject collection before recreating the same stable object names.
        unreal.SystemLibrary.collect_garbage()


def unregister_wait_callback(state):
    if state["handle"] is not None:
        unreal.unregister_slate_post_tick_callback(state["handle"])
        state["handle"] = None


def ensure_editor_region(actor_subsystem, region_spec):
    region_class = getattr(unreal, "LocationVolume", None)
    if region_class is None:
        raise RuntimeError("The installed engine does not expose ALocationVolume to Python")

    bounds = region_spec["bounds"]
    minimum = bounds["min"]
    maximum = bounds["max"]
    center = [(minimum[index] + maximum[index]) * 0.5 for index in range(3)]
    extent = [(maximum[index] - minimum[index]) * 0.5 for index in range(3)]
    region = actor_subsystem.spawn_actor_from_class(
        region_class,
        unreal.Vector(*center),
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    if region is None:
        raise RuntimeError("Could not spawn demo editor region")
    region.rename(region_spec["actorName"])
    region.set_actor_label(region_spec["actorLabel"])
    # ALocationVolume's default brush is 200 cm wide on each axis.
    region.set_actor_scale3d(unreal.Vector(*(value / 100.0 for value in extent)))
    return region


def configure_actor(actor, entry, data_layer_instances):
    actor.set_actor_label(entry["actorLabel"])
    actor.set_folder_path(entry["folder"])
    set_property(actor, "tags", [unreal.Name(tag) for tag in entry.get("actorTags", [])])
    properties = entry.get("properties", {})
    for property_name, property_value in properties.items():
        if property_name == "Description":
            property_value = unreal.Text(str(property_value))
        set_property(actor, property_name.lower(), property_value)
    actor.set_gameplay_tag_names([unreal.Name(tag) for tag in entry.get("gameplayTags", [])])

    visual_mesh = entry.get("visualMesh")
    if visual_mesh:
        assign_basic_shape_mesh(actor, visual_mesh, entry["actorName"])

    data_layer_subsystem = unreal.get_editor_subsystem(unreal.DataLayerEditorSubsystem)
    for layer_name in entry.get("dataLayers", []):
        if not data_layer_subsystem.add_actor_to_data_layer(actor, data_layer_instances[layer_name]):
            raise RuntimeError("Could not add {} to Data Layer {}".format(actor.get_name(), layer_name))


def spawn_environment_actor(actor_subsystem, entry):
    actor_class = resolve_unreal_class(entry["actorClass"])
    actor = actor_subsystem.spawn_actor_from_class(
        actor_class,
        unreal.Vector(*entry["location"]),
        unreal.Rotator(*entry["rotation"]),
    )
    if actor is None:
        raise RuntimeError("Could not spawn environment actor {}".format(entry["actorName"]))
    actor.rename(entry["actorName"])
    if actor.get_name() != entry["actorName"]:
        raise RuntimeError("Environment actor name did not resolve to {} (got {})".format(
            entry["actorName"], actor.get_name()))
    actor.set_actor_label(entry["actorLabel"])
    actor.set_folder_path(entry["folder"])
    return actor


def configure_floor(actor, floor_spec):
    actor.set_actor_scale3d(unreal.Vector(*floor_spec["scale"]))
    assign_basic_shape_mesh(actor, floor_spec["mesh"], floor_spec["actorName"])


def configure_directional_light(actor, light_spec):
    component = get_exact_component(actor, unreal.DirectionalLightComponent, light_spec["actorName"])
    component.set_editor_property("intensity", float(light_spec["intensity"]))
    component.set_editor_property("affects_world", True)
    component.set_editor_property("cast_shadows", bool(light_spec["castShadows"]))
    component.set_editor_property("mobility", unreal.ComponentMobility.STATIONARY)
    component.set_atmosphere_sun_light(bool(light_spec["atmosphereSunLight"]))


def get_captured_scene_source_type():
    source_type = getattr(unreal, "SkyLightSourceType", None)
    if source_type is None:
        raise RuntimeError("The installed engine does not expose SkyLightSourceType")
    captured_scene = getattr(source_type, "SLS_CAPTURED_SCENE", None)
    if captured_scene is None:
        captured_scene = getattr(source_type, "CAPTURED_SCENE", None)
    if captured_scene is None:
        raise RuntimeError("The installed engine does not expose the captured-scene Sky Light source type")
    return captured_scene


def configure_sky_light(actor, light_spec):
    component = get_exact_component(actor, unreal.SkyLightComponent, light_spec["actorName"])
    component.set_editor_property("intensity", float(light_spec["intensity"]))
    component.set_editor_property("affects_world", True)
    component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    component.set_editor_property("source_type", get_captured_scene_source_type())
    component.set_real_time_capture(bool(light_spec["realTimeCapture"]))


def configure_sky_atmosphere(actor, atmosphere_spec):
    component = get_exact_component(actor, unreal.SkyAtmosphereComponent, atmosphere_spec["actorName"])
    if component is None:
        raise RuntimeError("Could not resolve Sky Atmosphere component on {}".format(atmosphere_spec["actorName"]))
    actor.set_actor_scale3d(unreal.Vector(*atmosphere_spec["scale"]))


def ensure_visual_environment(actor_subsystem, environment_spec):
    folder = environment_spec["folder"]
    floor_spec = dict(environment_spec["floor"])
    floor_spec["folder"] = folder
    sun_spec = dict(environment_spec["directionalLight"])
    sun_spec["folder"] = folder
    sky_light_spec = dict(environment_spec["skyLight"])
    sky_light_spec["folder"] = folder
    sky_atmosphere_spec = dict(environment_spec["skyAtmosphere"])
    sky_atmosphere_spec["folder"] = folder

    floor = spawn_environment_actor(actor_subsystem, floor_spec)
    configure_floor(floor, floor_spec)

    sun = spawn_environment_actor(actor_subsystem, sun_spec)
    configure_directional_light(sun, sun_spec)

    sky_light = spawn_environment_actor(actor_subsystem, sky_light_spec)
    configure_sky_light(sky_light, sky_light_spec)

    sky_atmosphere = spawn_environment_actor(actor_subsystem, sky_atmosphere_spec)
    configure_sky_atmosphere(sky_atmosphere, sky_atmosphere_spec)


def regenerate_map(spec, level_subsystem, actor_subsystem):
    clear_generated_actors(actor_subsystem)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    data_layer_assets = {
        name: ensure_data_layer_asset(asset_tools, name) for name in spec["dataLayers"]
    }
    data_layer_instances = ensure_data_layer_instances(data_layer_assets)

    for entry in spec["actors"]:
        class_path = ACTOR_CLASSES[entry["actorClass"]]
        actor_class = unreal.load_class(None, class_path)
        if actor_class is None:
            raise RuntimeError("Could not load fixture class {}".format(class_path))
        transform = entry["sampleTransform"]
        location = unreal.Vector(*transform["location"])
        rotation = unreal.Rotator(*transform["rotation"])
        actor = actor_subsystem.spawn_actor_from_class(actor_class, location, rotation)
        if actor is None:
            raise RuntimeError("Could not spawn {}".format(entry["actorName"]))
        actor.rename(entry["actorName"])
        actor.set_actor_scale3d(unreal.Vector(*transform["scale"]))
        configure_actor(actor, entry, data_layer_instances)

    ensure_editor_region(actor_subsystem, spec["editorRegion"])
    ensure_visual_environment(actor_subsystem, spec["visualEnvironment"])

    if not level_subsystem.save_current_level():
        raise RuntimeError("Could not save overview map")
    unreal.log("Actor Metadata Overlay sample map generated: {}".format(MAP_PATH))


def wait_for_existing_generated_actors(spec, level_subsystem, actor_subsystem):
    required_names = get_required_generated_actor_names(spec)
    required_name_set = set(required_names)
    state = {"handle": None, "started": time.monotonic(), "attempts": 0, "completed": False}

    def on_post_tick(_delta_seconds):
        if state["completed"]:
            return
        state["attempts"] += 1
        try:
            current_names = {actor.get_name() for actor in actor_subsystem.get_all_level_actors()}
            if not required_name_set.issubset(current_names):
                if time.monotonic() - state["started"] > 60.0:
                    missing = sorted(required_name_set - current_names)
                    state["completed"] = True
                    unregister_wait_callback(state)
                    raise RuntimeError("Timed out waiting for the existing World Partition demo region actors; missing: {}".format(
                        ", ".join(missing)))
                return

            state["completed"] = True
            unregister_wait_callback(state)
            unreal.log("Existing demo actors ready: waitTargetCount={} attempts={} requiredNames={}".format(
                len(required_names), state["attempts"], ",".join(required_names)))
            regenerate_map(spec, level_subsystem, actor_subsystem)
        except Exception:
            if not state["completed"]:
                state["completed"] = True
                unregister_wait_callback(state)
            raise

    state["handle"] = unreal.register_slate_post_tick_callback(on_post_tick)


def main():
    spec = read_spec()
    get_required_generated_actor_names(spec)
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    map_exists = unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH)
    if map_exists:
        if not level_subsystem.load_level(MAP_PATH):
            raise RuntimeError("Could not load existing overview map")
    elif not level_subsystem.new_level(MAP_PATH, True):
        raise RuntimeError("Could not create partitioned overview map")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if map_exists:
        # Existing World Partition actors arrive on a later editor tick after
        # ALocationVolume::Load; wait for that state transition before deleting
        # and recreating stable actor names.
        wait_for_existing_generated_actors(spec, level_subsystem, actor_subsystem)
    else:
        regenerate_map(spec, level_subsystem, actor_subsystem)


if __name__ == "__main__":
    main()
