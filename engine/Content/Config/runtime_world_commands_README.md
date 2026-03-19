# Runtime Spawn Via `send_world_command`

This project now includes three native world commands:

- `SpawnAsset`
- `ClearSpawned`
- `RespawnFromConfig`

C++ implementation:

- `Source/Holodeck/ClientCommands/Public/SpawnAssetCommand.h`
- `Source/Holodeck/ClientCommands/Public/ClearSpawnedCommand.h`
- `Source/Holodeck/ClientCommands/Public/RespawnFromConfigCommand.h`
- `Source/Holodeck/Utils/Public/RuntimeRowSpawner.h`

Runtime config:

- `Content/Config/sonar_rows_runtime.json`

## One-Time Unreal Setup

1. Close Unreal Editor.
2. Open `Holodeck.uproject` or `Holodeck.sln` and let the C++ project compile.
3. Reopen the project in Unreal.
4. Open the `ExampleLevel` map.
5. In the `World Outliner`, add a `RuntimeRowSpawner` actor:
   `Place Actors` -> search for `RuntimeRowSpawner` -> drag it into the level.
6. Select the actor and set the following fields in `Details`:
   `Config Path = Content/Config/sonar_rows_runtime.json`
   `Config Path Is Absolute = false`
   `Apply On Begin Play = false`

Notes:

- The runtime config used by `RespawnFromConfig` is `Content/Config/sonar_rows_runtime.json`.
- Public-facing labels in this extract use English names such as `mine`, `torpedo`, and `anchor`.
- The actual Unreal mesh package names remain unchanged as `/Game/mina.mina`, `/Game/siluro.siluro`, and `/Game/ancora.ancora` to avoid breaking binary asset references.

## Python Usage from HoloOcean

```python
import holoocean

env = holoocean.make("YourScenario")

# Spawn a single asset
env.send_world_command(
    "SpawnAsset",
    num_params=[1200.0, -350.0, -800.0, 0.0, 0.0, 45.0, 1.0, 1.0, 1.0],
    string_params=["/Game/ancora.ancora", "anchor_01"]  # optional third string: "meters"
)
env.tick()

# Clear only runtime-spawned actors
env.send_world_command("ClearSpawned", num_params=[], string_params=[])
env.tick()

# Respawn from config (clears runtime-spawned actors and applies the JSON)
env.send_world_command(
    "RespawnFromConfig",
    num_params=[],
    string_params=["Content/Config/sonar_rows_runtime.json", "false"]
)
env.tick()
```

## Runtime JSON Format Summary

- `global.destroy_extra`: removes actors beyond the target count.
- `global.use_client_units`: if `true`, interprets positions in HoloOcean client meters.
- `rows.<name>.mesh_asset`: Unreal asset path (`/Game/...`).
- `rows.<name>.positions` and `rotations`: explicit transform lists.
- `rows.<name>.position_generation`: generates positions from `start`, `step`, and `count`.
- `rows.<name>.offset_location` and `offset_rotation`: final transform offsets.
- `rows.<name>.fit_scale_to_reference_actor`: scales the new asset to match a placeholder or template actor.
