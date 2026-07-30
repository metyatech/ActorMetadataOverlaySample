# Sync architecture

The synchronization direction is deliberately one-way:

```text
Public Sample (semantic spec and free fixtures)
        |
        |  explicit Dry Run, then explicit Apply
        v
Private Capture Host (private transforms, camera, lighting, and assets)
```

`Scripts/Sync-DemoToCaptureHost.ps1` records the Sample HEAD and clean state, validates the target, checks path traversal, creates a plan, records SHA-256 hashes, and backs up overwritten files under `.verification/` before an Apply. It never copies from the Capture Host back into the Sample.

The default mode is Dry Run. Deletes require `-AllowDelete`. The allowlist is limited to the free fixture plugin, `Demo/demo-spec.json`, the Sample Gameplay Tags config, and `Scripts/Capture/Apply-DemoSpec.py`. Paid plugin files, Deep Water Station content, private maps, build output, Marketing, and verification directories are prohibited.

`captureRole` is semantic. A private Capture Host supplies a separate `capture-layout.json` that maps those roles to private actors and transforms. The public specification never contains Deep Water Station transforms or camera data.
