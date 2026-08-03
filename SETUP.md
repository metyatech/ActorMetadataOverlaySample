# Setup

## Local development

1. Clone `ActorMetadataOverlaySample` and `EditorActorTagDisplayPlugin` next to each other.
2. Run `Scripts/Setup-Local.ps1` with the plugin repository path and selected engine version:

   ```powershell
   .\Scripts\Setup-Local.ps1 `
     -PluginSource '..\EditorActorTagDisplayPlugin' `
     -EngineVersion 5.6 `
     -Build
   ```

   For the UE 5.6 baseline project, keep `-EngineVersion 5.6 -Build` when opening the project normally. To use UE 5.7 or UE 5.8, explicitly open the project with that engine and run Setup-Local with the same engine version first. Rerun the setup command every time you switch engine versions.

3. Open `ActorMetadataSample.uproject` with the same engine version.
4. Open `/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview`.

The public repository keeps its `ActorMetadataOverlaySample` name, while the internal Unreal project is `ActorMetadataSample` so it stays within Unreal's 20-character project-name limit. The project explicitly disables the engine `AndroidFileServer` plugin; do not add its generated settings or `SecurityToken` to `Config/DefaultEngine.ini`. On the exact overview map, the Fixture Editor module loads `AMO_DemoRegion` once and exposes all seven fixtures through the normal editor actor iterator.

The setup script creates a fresh ignored copy in `Plugins/EditorActorTagDisplay/`, adjusts only the copied descriptor and generated build output, and never edits the source plugin descriptor or repository. The fixture plugin remains free sample source and has only Engine-standard module dependencies.

The source workflow compiles C++ modules, so Visual Studio or another supported Unreal C++ toolchain is required. A precompiled Sample release ZIP is not provided at this time.

## Fab customer

1. Download the matching `Actor Metadata Overlay` plugin package from Fab.
2. Copy its `EditorActorTagDisplay` directory into this project's `Plugins/` directory.
3. Open `ActorMetadataSample.uproject` with the matching Unreal Engine version.
4. Open `/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview`.

The purchased plugin is intentionally absent from Git. The free `ActorMetadataOverlayDemoFixtures` plugin is included in source and does not depend on the paid plugin.

## Regenerating the map

Map generation is explicit. From an unattended Unreal Editor command line, run `Scripts/Build-DemoMap.py`. Do not add `Content/Python/init_unreal.py` or another startup hook. The script reads `Demo/demo-spec.json`, creates a World Partition map, creates the `Gameplay` and `Night` Data Layers, and spawns the fixture actors.
