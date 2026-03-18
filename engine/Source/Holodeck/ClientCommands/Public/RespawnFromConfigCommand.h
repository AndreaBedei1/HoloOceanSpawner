#pragma once

#include "Command.h"

#include "RespawnFromConfigCommand.generated.h"

/**
 * RespawnFromConfigCommand
 *
 * String params:
 * - [config_path] optional
 * - [absolute] optional bool-like string
 *
 * If no params are provided, the spawner uses its own ConfigPath property.
 */
UCLASS()
class HOLODECK_API URespawnFromConfigCommand : public UCommand {
	GENERATED_BODY()

public:
	void Execute() override;
};

