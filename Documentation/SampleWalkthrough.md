# Sample walkthrough

1. Open `ActorMetadataSample.uproject` after installing the matching paid plugin.
2. Open `/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview` and leave the viewport in `Lit` mode.
3. Start in the central plaza. The seven colored stations are the canonical fixture layout: Loot, Spawn, Quest, Navigation, Zone, Filtered, and Distance.
4. Confirm that the editor-only `AMO_DemoRegion` is loaded, its wireframe is hidden in the normal viewport, and the seven fixture actors are available before applying the documented overlay rules.
5. In the viewport Show menu, choose Actor Metadata Overlay > Selected Actors and select `Loot Crate A`.
6. Choose All Matching Actors to see the point and zone rules together, then choose Off and confirm that no overlay returns by itself.
7. Turn on Game View and confirm that editor overlays are hidden. Turn Game View off to restore them.
8. Inspect `Audio Zone — Courtyard` for the green bounding box and Radius line.
9. Move the view to the separate distance lane. The three markers demonstrate `50 m` reference, `100 m` default draw distance, and `200 m` far-actor filtering.
10. Inspect `Filtered Debug Actor`; its `OverlayHidden` tag prevents a match.

The floor, lighting, sky, plaza, stations, lane, signage, and sample materials are public-sample presentation aids only. They are not copied to the private Capture Host and do not change the paid plugin's overlay rules.

The product default is Selected Actors. The sample contains no timer, ticker, startup Python, capture diagnostics, or script that changes Display Mode.
