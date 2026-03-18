#pragma once

#include "Command.h"

#include "ClearSpawnedCommand.generated.h"

/**
 * ClearSpawnedCommand
 *
 * No params expected. Destroys actors spawned by RuntimeRowSpawner.
 */
UCLASS()
class HOLODECK_API UClearSpawnedCommand : public UCommand {
	GENERATED_BODY()

public:
	void Execute() override;
};

