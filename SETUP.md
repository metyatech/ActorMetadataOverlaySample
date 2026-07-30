# Setup

## Local development

1. Clone `ActorMetadataOverlaySample` and `EditorActorTagDisplayPlugin` next to each other.
2. Run `Scripts/Setup-Local.ps1` with the plugin repository path and engine version:

   ```powershell
   .\Scripts\Setup-Local.ps1 -PluginSource '..\EditorActorTagDisplayPlugin' -EngineVersion 5.6
   ```

3. Open `ActorMetadataOverlaySample.uproject` with the same engine version.
4. Open `/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview`.

The setup script copies the paid plugin into the ignored `Plugins/EditorActorTagDisplay/` directory. It does not edit the plugin source. The fixture plugin remains free sample source and has only Engine-standard module dependencies.

The source workflow compiles C++ modules, so Visual Studio or another supported Unreal C++ toolchain is required. A precompiled Sample release ZIP is not provided at this time.

## Fab customer

1. Download the matching `Actor Metadata Overlay` plugin package from Fab.
2. Copy its `EditorActorTagDisplay` directory into this project's `Plugins/` directory.
3. Open `ActorMetadataOverlaySample.uproject` with the matching Unreal Engine version.
4. Open `/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview`.

The purchased plugin is intentionally absent from Git. The free `ActorMetadataOverlayDemoFixtures` plugin is included in source and does not depend on the paid plugin.

## Regenerating the map

Map generation is explicit. From an unattended Unreal Editor command line, run `Scripts/Build-DemoMap.py`. Do not add `Content/Python/init_unreal.py` or another startup hook. The script reads `Demo/demo-spec.json`, creates a World Partition map, creates the `Gameplay` and `Night` Data Layers, and spawns the fixture actors.
