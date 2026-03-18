#pragma once

#include "Command.h"

#include "SpawnAssetCommand.generated.h"

/**
 * SpawnAssetCommand
 *
 * Number parameters:
 * [x, y, z, roll, pitch, yaw, sx, sy, sz]
 *
 * String parameters:
 * [mesh_asset_path, actor_label(optional), units(optional: "meters"|"client")]
 */
UCLASS()
class HOLODECK_API USpawnAssetCommand : public UCommand {
	GENERATED_BODY()

public:
	void Execute() override;
};

