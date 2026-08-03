"""Build the public overview map when explicitly run inside Unreal Editor."""

import json
import os
import time

import unreal


MAP_PATH = "/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview"
MATERIAL_ROOT = "/Game/ActorMetadataOverlayDemo/Visuals/Materials"
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
    "ATextRenderActor": "TextRenderActor",
    "ACameraActor": "CameraActor",
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


def presentation_actor_entries(spec):
    presentation = spec.get("presentation")
    if not isinstance(presentation, dict):
        raise RuntimeError("demo-spec.json key 'presentation' must be an object")

    plaza = presentation.get("plaza")
    if not isinstance(plaza, dict):
        raise RuntimeError("demo-spec.json presentation.plaza must be an object")
    yield plaza["floor"]
    for entry in plaza.get("borders", []) + plaza.get("accentLines", []):
        yield entry
    yield plaza["backdrop"]

    stations = spec.get("stations")
    if not isinstance(stations, list) or len(stations) != 7:
        raise RuntimeError("demo-spec.json key 'stations' must contain exactly 7 entries")
    for entry in stations:
        yield entry

    lane = spec.get("distanceLane")
    if not isinstance(lane, dict):
        raise RuntimeError("demo-spec.json key 'distanceLane' must be an object")
    yield lane["floor"]
    for entry in lane.get("borders", []):
        yield entry
    yield lane["boundary"]
    for entry in lane.get("markers", []) + lane.get("labels", []):
        yield entry

    signage = spec.get("signage")
    if not isinstance(signage, dict) or not isinstance(signage.get("texts"), list):
        raise RuntimeError("demo-spec.json key 'signage.texts' must be a list")
    for entry in signage["texts"]:
        yield entry

    camera = spec.get("overviewCamera")
    if not isinstance(camera, dict):
        raise RuntimeError("demo-spec.json key 'overviewCamera' must be an object")
    yield camera


def get_required_generated_actor_names(spec):
    actor_entries = spec.get("actors")
    if not isinstance(actor_entries, list) or len(actor_entries) != 7:
        raise RuntimeError("demo-spec.json key 'actors' must contain exactly 7 fixture entries")

    names = []
    for index, entry in enumerate(actor_entries):
        if not isinstance(entry, dict):
            raise RuntimeError("demo-spec.json actors[{}] must be an object".format(index))
        actor_name = entry.get("actorName")
        if not isinstance(actor_name, str) or not actor_name.strip():
            raise RuntimeError("demo-spec.json actors[{}].actorName must be non-empty".format(index))
        names.append(actor_name)

    region_spec = spec.get("editorRegion")
    if not isinstance(region_spec, dict) or region_spec.get("actorName") != "AMO_DemoRegion":
        raise RuntimeError("demo-spec.json editorRegion.actorName must be AMO_DemoRegion")
    names.append(region_spec["actorName"])

    visual_environment = spec.get("visualEnvironment")
    if not isinstance(visual_environment, dict):
        raise RuntimeError("demo-spec.json key 'visualEnvironment' must be an object")
    for key in VISUAL_ENVIRONMENT_KEYS:
        entry = visual_environment.get(key)
        if not isinstance(entry, dict) or not entry.get("actorName", "").startswith("AMO_Environment_"):
            raise RuntimeError("demo-spec.json visualEnvironment.{} must define an AMO_Environment_ actor".format(key))
        names.append(entry["actorName"])

    for index, entry in enumerate(presentation_actor_entries(spec)):
        if not isinstance(entry, dict):
            raise RuntimeError("demo-spec.json presentation actor {} must be an object".format(index))
        actor_name = entry.get("actorName")
        if not isinstance(actor_name, str) or not actor_name.startswith("AMO_Environment_"):
            raise RuntimeError("Presentation actor {} must use the AMO_Environment_ prefix".format(index))
        names.append(actor_name)

    duplicate_names = sorted({name for name in names if names.count(name) > 1})
    if duplicate_names:
        raise RuntimeError("demo-spec.json generated actor names must be unique: {}".format(", ".join(duplicate_names)))
    return names


def set_property(actor, name, value):
    try:
        actor.set_editor_property(name, value)
    except Exception as error:
        raise RuntimeError("Could not set {} on {}: {}".format(name, actor.get_name(), error))


def spec_rotator(values):
    return unreal.Rotator(
        pitch=float(values[0]),
        yaw=float(values[1]),
        roll=float(values[2]),
    )


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


def load_sample_material(material_name):
    asset_path = MATERIAL_ROOT + "/" + material_name
    material = unreal.EditorAssetLibrary.load_asset(asset_path)
    if material is None or not isinstance(material, unreal.MaterialInterface):
        raise RuntimeError("Could not load required Sample material {}".format(asset_path))
    return material


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


def assign_material(component, material_name, description):
    material = load_sample_material(material_name)
    component.set_material(0, material)
    assigned_material = component.get_material(0)
    expected_path = MATERIAL_ROOT + "/" + material_name
    assigned_path = assigned_material.get_path_name() if assigned_material is not None else "<none>"
    if assigned_material is None or not (assigned_path == expected_path or assigned_path.startswith(expected_path + ".")):
        raise RuntimeError("Could not assign Sample material {} to {} (got {})".format(
            material_name, description, assigned_path))


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
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        raise RuntimeError("Could not save Data Layer asset {}".format(asset_path))
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


def create_material_expression(material, expression_class, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, expression_class, x, y)


def ensure_master_material(master, master_spec):
    set_property(master, "blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    set_property(master, "shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    set_property(master, "two_sided", False)

    required_parameters = set(master_spec["parameters"].keys())
    expression_parameters = {
        str(name) for name in unreal.MaterialEditingLibrary.get_vector_parameter_names(master)
    }
    expression_parameters.update(
        str(name) for name in unreal.MaterialEditingLibrary.get_scalar_parameter_names(master)
    )

    if not required_parameters.issubset(expression_parameters):
        unreal.MaterialEditingLibrary.delete_all_material_expressions(master)
        base = create_material_expression(master, unreal.MaterialExpressionVectorParameter, -800, -320)
        base.set_editor_property("parameter_name", "BaseColor")
        base.set_editor_property("default_value", unreal.LinearColor(0.18, 0.22, 0.28, 1.0))
        roughness = create_material_expression(master, unreal.MaterialExpressionScalarParameter, -800, -80)
        roughness.set_editor_property("parameter_name", "Roughness")
        roughness.set_editor_property("default_value", 0.72)
        metallic = create_material_expression(master, unreal.MaterialExpressionScalarParameter, -800, 160)
        metallic.set_editor_property("parameter_name", "Metallic")
        metallic.set_editor_property("default_value", 0.0)
        emissive_color = create_material_expression(master, unreal.MaterialExpressionVectorParameter, -800, 400)
        emissive_color.set_editor_property("parameter_name", "EmissiveColor")
        emissive_color.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
        emissive_strength = create_material_expression(master, unreal.MaterialExpressionScalarParameter, -800, 640)
        emissive_strength.set_editor_property("parameter_name", "EmissiveStrength")
        emissive_strength.set_editor_property("default_value", 0.0)
        emissive_multiply = create_material_expression(master, unreal.MaterialExpressionMultiply, -280, 480)

        unreal.MaterialEditingLibrary.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
        unreal.MaterialEditingLibrary.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
        unreal.MaterialEditingLibrary.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)
        unreal.MaterialEditingLibrary.connect_material_expressions(emissive_color, "", emissive_multiply, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(emissive_strength, "", emissive_multiply, "B")
        unreal.MaterialEditingLibrary.connect_material_property(emissive_multiply, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    unreal.MaterialEditingLibrary.recompile_material(master)
    if not unreal.EditorAssetLibrary.save_loaded_asset(master):
        raise RuntimeError("Could not save master material {}".format(master.get_path_name()))
    return master


def ensure_material_instance(asset_tools, master, instance_spec):
    name = instance_spec["name"]
    asset_path = instance_spec["path"]
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        instance = unreal.EditorAssetLibrary.load_asset(asset_path)
    else:
        factory = unreal.MaterialInstanceConstantFactoryNew()
        instance = asset_tools.create_asset(name, MATERIAL_ROOT, unreal.MaterialInstanceConstant, factory)
    if instance is None or not isinstance(instance, unreal.MaterialInstanceConstant):
        raise RuntimeError("Could not create or load Material Instance {}".format(asset_path))

    instance.set_editor_property("parent", master)
    if instance.get_editor_property("parent") != master:
        raise RuntimeError("Material Instance {} has an unexpected parent".format(asset_path))
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        instance, "BaseColor", unreal.LinearColor(*instance_spec["BaseColor"]))
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
        instance, "Roughness", float(instance_spec["Roughness"]))
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
        instance, "Metallic", float(instance_spec["Metallic"]))
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        instance, "EmissiveColor", unreal.LinearColor(*instance_spec["EmissiveColor"]))
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
        instance, "EmissiveStrength", float(instance_spec["EmissiveStrength"]))
    if not unreal.EditorAssetLibrary.save_loaded_asset(instance):
        raise RuntimeError("Could not save Material Instance {}".format(asset_path))
    return instance


def ensure_material_system(spec, asset_tools):
    materials = spec.get("materials")
    if not isinstance(materials, dict) or materials.get("root") != MATERIAL_ROOT:
        raise RuntimeError("demo-spec.json materials.root must be {}".format(MATERIAL_ROOT))
    master_spec = materials.get("master")
    if not isinstance(master_spec, dict):
        raise RuntimeError("demo-spec.json materials.master is missing")
    master_path = master_spec["path"]
    if unreal.EditorAssetLibrary.does_asset_exist(master_path):
        master = unreal.EditorAssetLibrary.load_asset(master_path)
    else:
        master = asset_tools.create_asset(master_spec["name"], MATERIAL_ROOT, unreal.Material, unreal.MaterialFactoryNew())
    if master is None or not isinstance(master, unreal.Material):
        raise RuntimeError("Could not create or load master material {}".format(master_path))
    ensure_master_material(master, master_spec)

    instances = {}
    for instance_spec in materials.get("instances", []):
        instances[instance_spec["name"]] = ensure_material_instance(asset_tools, master, instance_spec)
    if len(instances) != 13:
        raise RuntimeError("demo-spec.json must define exactly 13 Material Instances")
    return master, instances


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
        if actor_name.startswith("AMO_") or actor.get_actor_label().startswith("Loot Crate"):
            temporary_name = "__AMO_DELETE_{}_{}".format(actor_name, index)
            actor.rename(temporary_name)
            if not actor_subsystem.destroy_actor(actor):
                raise RuntimeError("Could not remove generated actor {}".format(actor_name))
            destroyed_count += 1
    if destroyed_count:
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
    region = actor_subsystem.spawn_actor_from_class(region_class, unreal.Vector(*center), unreal.Rotator(0.0, 0.0, 0.0))
    if region is None:
        raise RuntimeError("Could not spawn demo editor region")
    region.rename(region_spec["actorName"])
    region.set_actor_label(region_spec["actorLabel"])
    region.set_actor_scale3d(unreal.Vector(*(value / 100.0 for value in extent)))
    return region


def configure_actor(actor, entry, data_layer_instances):
    actor.set_actor_label(entry["actorLabel"])
    actor.set_folder_path(entry["folder"])
    set_property(actor, "tags", [unreal.Name(tag) for tag in entry.get("actorTags", [])])
    for property_name, property_value in entry.get("properties", {}).items():
        if property_name == "Description":
            property_value = unreal.Text(str(property_value))
        set_property(actor, property_name.lower(), property_value)
    actor.set_gameplay_tag_names([unreal.Name(tag) for tag in entry.get("gameplayTags", [])])

    visual_mesh = entry.get("visualMesh")
    if visual_mesh:
        component = assign_basic_shape_mesh(actor, visual_mesh, entry["actorName"])
        material_name = entry.get("visualMaterial")
        if not material_name:
            raise RuntimeError("Fixture {} is missing visualMaterial".format(entry["actorName"]))
        assign_material(component, material_name, entry["actorName"])

    data_layer_subsystem = unreal.get_editor_subsystem(unreal.DataLayerEditorSubsystem)
    for layer_name in entry.get("dataLayers", []):
        if not data_layer_subsystem.add_actor_to_data_layer(actor, data_layer_instances[layer_name]):
            raise RuntimeError("Could not add {} to Data Layer {}".format(actor.get_name(), layer_name))


def spawn_environment_actor(actor_subsystem, entry, folder):
    actor_class = resolve_unreal_class(entry["actorClass"])
    actor = actor_subsystem.spawn_actor_from_class(
        actor_class, unreal.Vector(*entry["location"]), spec_rotator(entry["rotation"]))
    if actor is None:
        raise RuntimeError("Could not spawn environment actor {}".format(entry["actorName"]))
    actor.rename(entry["actorName"])
    if actor.get_name() != entry["actorName"]:
        raise RuntimeError("Environment actor name did not resolve to {} (got {})".format(
            entry["actorName"], actor.get_name()))
    actor.set_actor_label(entry["actorLabel"])
    actor.set_folder_path(folder)
    if "scale" in entry:
        actor.set_actor_scale3d(unreal.Vector(*entry["scale"]))
    return actor


def configure_static_mesh_actor(actor, entry):
    component = get_exact_component(actor, unreal.StaticMeshComponent, entry["actorName"])
    assign_basic_shape_mesh(actor, entry["mesh"], entry["actorName"])
    assign_material(component, entry["material"], entry["actorName"])


def configure_text_actor(actor, entry):
    component = get_exact_component(actor, unreal.TextRenderComponent, entry["actorName"])
    set_property(component, "text", unreal.Text(entry["text"]))
    set_property(component, "world_size", float(entry.get("textSize", 50.0)))
    horizontal_alignment = component.get_editor_property("horizontal_alignment")
    vertical_alignment = component.get_editor_property("vertical_alignment")
    set_property(component, "horizontal_alignment", type(horizontal_alignment).EHTA_CENTER)
    set_property(component, "vertical_alignment", type(vertical_alignment).EVRTA_TEXT_CENTER)
    set_property(component, "text_render_color", unreal.Color(224, 242, 250, 255))


def configure_camera_actor(actor, entry):
    set_property(actor, "is_spatially_loaded", False)
    component = get_exact_component(actor, unreal.CameraComponent, entry["actorName"])
    set_property(component, "field_of_view", float(entry["fov"]))


def ensure_visual_environment(actor_subsystem, environment_spec, folder):
    floor = spawn_environment_actor(actor_subsystem, environment_spec["floor"], folder)
    configure_static_mesh_actor(floor, environment_spec["floor"])

    sun = spawn_environment_actor(actor_subsystem, environment_spec["directionalLight"], folder)
    light_spec = environment_spec["directionalLight"]
    component = get_exact_component(sun, unreal.DirectionalLightComponent, light_spec["actorName"])
    set_property(component, "intensity", float(light_spec["intensity"]))
    set_property(component, "affects_world", True)
    set_property(component, "cast_shadows", bool(light_spec["castShadows"]))
    set_property(component, "mobility", unreal.ComponentMobility.MOVABLE)
    component.set_atmosphere_sun_light(bool(light_spec["atmosphereSunLight"]))

    sky_light = spawn_environment_actor(actor_subsystem, environment_spec["skyLight"], folder)
    sky_spec = environment_spec["skyLight"]
    sky_component = get_exact_component(sky_light, unreal.SkyLightComponent, sky_spec["actorName"])
    set_property(sky_component, "intensity", float(sky_spec["intensity"]))
    set_property(sky_component, "affects_world", True)
    set_property(sky_component, "mobility", unreal.ComponentMobility.MOVABLE)
    source_type = getattr(unreal.SkyLightSourceType, "SLS_CAPTURED_SCENE", None)
    if source_type is None:
        source_type = unreal.SkyLightSourceType.CAPTURED_SCENE
    set_property(sky_component, "source_type", source_type)
    sky_component.set_real_time_capture(bool(sky_spec["realTimeCapture"]))

    atmosphere = spawn_environment_actor(actor_subsystem, environment_spec["skyAtmosphere"], folder)
    get_exact_component(atmosphere, unreal.SkyAtmosphereComponent, environment_spec["skyAtmosphere"]["actorName"])


def ensure_presentation(actor_subsystem, spec, folder):
    for entry in presentation_actor_entries(spec):
        unreal.log("AMO_PRESENTATION_SPAWN {} class={}".format(entry["actorName"], entry["actorClass"]))
        actor = spawn_environment_actor(actor_subsystem, entry, folder)
        if entry["actorClass"] == "AStaticMeshActor":
            configure_static_mesh_actor(actor, entry)
        elif entry["actorClass"] == "ATextRenderActor":
            configure_text_actor(actor, entry)
        elif entry["actorClass"] == "ACameraActor":
            configure_camera_actor(actor, entry)
        else:
            raise RuntimeError("Unsupported presentation actor class {}".format(entry["actorClass"]))


def regenerate_map(spec, level_subsystem, actor_subsystem):
    clear_generated_actors(actor_subsystem)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    ensure_material_system(spec, asset_tools)
    data_layer_assets = {name: ensure_data_layer_asset(asset_tools, name) for name in spec["dataLayers"]}
    data_layer_instances = ensure_data_layer_instances(data_layer_assets)

    for entry in spec["actors"]:
        class_path = ACTOR_CLASSES[entry["actorClass"]]
        actor_class = unreal.load_class(None, class_path)
        if actor_class is None:
            raise RuntimeError("Could not load fixture class {}".format(class_path))
        transform = entry["sampleTransform"]
        actor = actor_subsystem.spawn_actor_from_class(
            actor_class, unreal.Vector(*transform["location"]), spec_rotator(transform["rotation"]))
        if actor is None:
            raise RuntimeError("Could not spawn {}".format(entry["actorName"]))
        actor.rename(entry["actorName"])
        actor.set_actor_scale3d(unreal.Vector(*transform["scale"]))
        configure_actor(actor, entry, data_layer_instances)

    ensure_editor_region(actor_subsystem, spec["editorRegion"])
    folder = spec["visualEnvironment"]["folder"]
    ensure_visual_environment(actor_subsystem, spec["visualEnvironment"], folder)
    ensure_presentation(actor_subsystem, spec, folder)

    if not level_subsystem.save_current_level():
        raise RuntimeError("Could not save overview map")
    unreal.log("Actor Metadata Overlay polished sample map generated: {}".format(MAP_PATH))


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
                if state["attempts"] >= 30 and any(name.startswith("AMO_") for name in current_names):
                    state["completed"] = True
                    unregister_wait_callback(state)
                    unreal.log("Existing demo actor set differs from demo-spec.json; rebuilding polished sample map")
                    regenerate_map(spec, level_subsystem, actor_subsystem)
                    return
                if time.monotonic() - state["started"] > 120.0:
                    missing = sorted(required_name_set - current_names)
                    state["completed"] = True
                    unregister_wait_callback(state)
                    raise RuntimeError("Timed out waiting for generated actors; missing: {}".format(", ".join(missing)))
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
        wait_for_existing_generated_actors(spec, level_subsystem, actor_subsystem)
    else:
        regenerate_map(spec, level_subsystem, actor_subsystem)


if __name__ == "__main__":
    main()
