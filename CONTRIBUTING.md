# Contributing

Keep the sample lightweight and free of third-party content. Changes to actor metadata, rules, Gameplay Tags, Data Layers, or map layout must update `Demo/demo-spec.json` and the relevant documentation together.

Before submitting a change:

1. Run `Scripts/Verify-Sample.ps1` after generating the map.
2. Build the fixture plugin with the supported engine version.
3. Confirm that the paid plugin remains ignored and untracked.
4. Run the sync script in Dry Run mode against a test target.
