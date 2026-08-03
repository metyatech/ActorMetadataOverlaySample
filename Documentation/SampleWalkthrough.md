# Sample walkthrough

1. Open `ActorMetadataSample.uproject` after installing the matching paid plugin.
2. Open `/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview`.
3. Confirm that the editor-only `AMO_DemoRegion` is loaded and the seven fixture actors are available before applying the documented overlay rules.
4. In the viewport Show menu, choose Actor Metadata Overlay > Selected Actors and select `Loot Crate A`.
5. Choose All Matching Actors to see the point and zone rules together.
6. Choose Off, wait a few seconds, and confirm that no overlay returns by itself.
7. Turn on Game View and confirm that editor overlays are hidden. Turn Game View off to restore them.
8. Inspect `Audio Zone — Courtyard` for the green bounding box and Radius line.
9. Move the view toward `Far Distance Actor` or temporarily increase the global distance to compare filtering.
10. Inspect `Filtered Debug Actor`; its `OverlayHidden` tag prevents a match.

The product default is Selected Actors. The sample contains no timer, ticker, startup Python, capture diagnostics, or script that changes Display Mode.
