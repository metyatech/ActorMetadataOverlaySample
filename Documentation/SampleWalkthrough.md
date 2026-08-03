# Sample walkthrough

1. Open `ActorMetadataSample.uproject` after installing the matching paid plugin.
2. Open `/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview`.
3. Leave the viewport in `Lit` mode. The map opens as a bright, neutral test lane using only Engine standard actors and Basic Shapes, with the fixture silhouettes easy to distinguish.
4. Confirm that the editor-only `AMO_DemoRegion` is loaded, its wireframe is hidden in the normal viewport, and the seven fixture actors are available before applying the documented overlay rules.
5. In the viewport Show menu, choose Actor Metadata Overlay > Selected Actors and select `Loot Crate A`.
6. Choose All Matching Actors to see the point and zone rules together.
7. Choose Off, wait a few seconds, and confirm that no overlay returns by itself.
8. Turn on Game View and confirm that editor overlays are hidden. Turn Game View off to restore them.
9. Inspect `Audio Zone — Courtyard` for the green bounding box and Radius line.
10. Move the view toward `Far Distance Actor` or temporarily increase the global distance to compare filtering.
11. Inspect `Filtered Debug Actor`; its `OverlayHidden` tag prevents a match.

The floor, lighting, and sky are public-sample presentation aids only. They are not copied to the private Capture Host and do not change the paid plugin's overlay rules.

The product default is Selected Actors. The sample contains no timer, ticker, startup Python, capture diagnostics, or script that changes Display Mode.
