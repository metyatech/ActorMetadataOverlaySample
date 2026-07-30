"""Build the public overview map when explicitly run inside Unreal Editor."""

import json
import os

import unreal


MAP_PATH = "/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview"
FIXTURE_MODULE = "ActorMetadataOverlayDemoFixtures"
ACTOR_CLASSES = {
    "AActorMetadataOverlayDemoActor": "/Script/ActorMetadataOverlayDemoFixtures.ActorMetadataOverlayDemoActor",
    "AActorMetadataOverlayDemoZone": "/Script/ActorMetadataOverlayDemoFixtures.ActorMetadataOverlayDemoZone",
}


def project_file(relative_path):
    return os.path.join(unreal.Paths.project_dir(), relative_path.replace("/", os.sep))


def read_spec():
    with open(project_file("Demo/demo-spec.json"), "r", encoding="utf-8") as handle:
        return json.load(handle)


def set_property(actor, name, value):
    try:
        actor.set_editor_property(name, value)
    except Exception as error:
        raise RuntimeError("Could not set {} on {}: {}".format(name, actor.get_name(), error))


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
    for actor in list(actor_subsystem.get_all_level_actors()):
        if actor.get_name().startswith("AMO_") or actor.get_actor_label().startswith("Loot Crate"):
            actor_subsystem.destroy_actor(actor)


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

    data_layer_subsystem = unreal.get_editor_subsystem(unreal.DataLayerEditorSubsystem)
    for layer_name in entry.get("dataLayers", []):
        if not data_layer_subsystem.add_actor_to_data_layer(actor, data_layer_instances[layer_name]):
            raise RuntimeError("Could not add {} to Data Layer {}".format(actor.get_name(), layer_name))


def main():
    spec = read_spec()
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        if not level_subsystem.load_level(MAP_PATH):
            raise RuntimeError("Could not load existing overview map")
    elif not level_subsystem.new_level(MAP_PATH, True):
        raise RuntimeError("Could not create partitioned overview map")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
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

    if not level_subsystem.save_current_level():
        raise RuntimeError("Could not save overview map")
    unreal.log("Actor Metadata Overlay sample map generated: {}".format(MAP_PATH))


if __name__ == "__main__":
    main()
