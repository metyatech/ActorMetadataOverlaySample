"""Apply public semantic demo metadata to a private capture layout.

This module intentionally does not run on startup. Invoke it explicitly from
an Unreal Editor command line with a private capture-layout.json path.
"""

import json
import os
import sys

import unreal


def _project_file(relative_path):
    return os.path.join(unreal.Paths.project_dir(), relative_path.replace("/", os.sep))


def _load_json(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def _set(actor, name, value):
    actor.set_editor_property(name, value)


def apply_demo_spec(spec_path, capture_layout_path):
    spec = _load_json(spec_path)
    layout = _load_json(capture_layout_path)
    layout_by_role = layout.get("roles", layout)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors_by_name = {actor.get_name(): actor for actor in actor_subsystem.get_all_level_actors()}
    data_layer_subsystem = getattr(unreal, "DataLayerEditorSubsystem", None)
    if data_layer_subsystem is not None:
        data_layer_subsystem = unreal.get_editor_subsystem(data_layer_subsystem)

    applied = []
    for entry in spec["actors"]:
        role = entry["captureRole"]
        private_entry = layout_by_role.get(role)
        if private_entry is None:
            raise RuntimeError("Capture layout is missing role {}".format(role))
        actor_name = private_entry.get("actorName", entry["actorName"])
        actor = actors_by_name.get(actor_name)
        if actor is None:
            raise RuntimeError("Capture layout actor is not loaded: {}".format(actor_name))
        actor.set_actor_label(entry["actorLabel"])
        actor.set_folder_path(entry["folder"])
        _set(actor, "tags", [unreal.Name(tag) for tag in entry.get("actorTags", [])])
        for property_name, property_value in entry.get("properties", {}).items():
            if property_name != "Description":
                _set(actor, property_name.lower(), property_value)
        if hasattr(actor, "set_gameplay_tag_names"):
            actor.set_gameplay_tag_names([unreal.Name(tag) for tag in entry.get("gameplayTags", [])])
        if data_layer_subsystem is not None:
            for layer_name in entry.get("dataLayers", []):
                instance = data_layer_subsystem.get_data_layer_instance(unreal.Name(layer_name))
                if instance is not None:
                    data_layer_subsystem.add_actor_to_data_layer(actor, instance)
        if "transform" in private_entry:
            transform = private_entry["transform"]
            actor.set_actor_location(unreal.Vector(*transform["location"]), False, False)
            actor.set_actor_rotation(unreal.Rotator(*transform["rotation"]), False)
        applied.append(role)
    unreal.log("Applied {} demo roles from public spec".format(len(applied)))
    return applied


def main():
    if len(sys.argv) != 2:
        raise RuntimeError("Usage: Apply-DemoSpec.py <private-capture-layout.json>")
    spec_path = _project_file("Demo/demo-spec.json")
    apply_demo_spec(spec_path, os.path.abspath(sys.argv[1]))


if __name__ == "__main__":
    main()
