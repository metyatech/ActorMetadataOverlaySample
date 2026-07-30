# Sample walkthrough

1. Open `ActorMetadataOverlaySample.uproject` after installing the matching paid plugin.
2. Open `/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview`.
3. In the viewport Show menu, choose Actor Metadata Overlay > Selected Actors and select `Loot Crate A`.
4. Choose All Matching Actors to see the point and zone rules together.
5. Choose Off, wait a few seconds, and confirm that no overlay returns by itself.
6. Turn on Game View and confirm that editor overlays are hidden. Turn Game View off to restore them.
7. Inspect `Audio Zone — Courtyard` for the green bounding box and Radius line.
8. Move the view toward `Far Distance Actor` or temporarily increase the global distance to compare filtering.
9. Inspect `Filtered Debug Actor`; its `OverlayHidden` tag prevents a match.

The product default is Selected Actors. The sample contains no timer, ticker, startup Python, capture diagnostics, or script that changes Display Mode.
