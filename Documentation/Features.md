# Features shown by the sample

The overview map is intentionally small so a customer can inspect each behavior without a content dependency.

| Feature | Sample evidence |
| --- | --- |
| Actor Tags | Point actors use `Inspectable`, `Loot`, `Enemy`, `Spawn`, `Quest`, `Marker`, `Navigation`, and `OverlayHidden`. |
| Gameplay Tags | Fixture actors implement `IGameplayTagAssetInterface`; tags are defined in the sample config. |
| Public property tokens | `State`, `Priority`, and `Radius` are editable properties used by the project rules. |
| Folders | Actors are grouped under `ActorMetadataOverlayDemo/...` folders. |
| Data Layers | `Gameplay` and `Night` are real World Partition Data Layers created by the map builder. |
| Bounding boxes | The Zone Actors rule enables the box for `Audio Zone — Courtyard`. |
| Distance filtering | `Far Distance Actor` is placed beyond the default 10,000-unit limit. |
| Rule exclusion | `Filtered Debug Actor` carries `OverlayHidden`, which the two rules exclude. |
| Display modes | The initial state is Selected Actors; the viewport Show menu supports Selected, All, and Off. |
