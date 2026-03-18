#include "ClearSpawnedCommand.h"

#include "Holodeck.h"
#include "RuntimeRowSpawner.h"

void UClearSpawnedCommand::Execute() {
	UWorld* World = Target ? Target->GetWorld() : nullptr;
	if (!World) {
		UE_LOG(LogHolodeck, Error, TEXT("ClearSpawnedCommand: world is nullptr."));
		return;
	}

	ARuntimeRowSpawner* Spawner = ARuntimeRowSpawner::FindOrCreateInWorld(World);
	if (!Spawner) {
		UE_LOG(LogHolodeck, Error, TEXT("ClearSpawnedCommand: unable to get spawner."));
		return;
	}

	const int32 Destroyed = Spawner->ClearSpawned();
	UE_LOG(
		LogHolodeck,
		Log,
		TEXT("ClearSpawnedCommand: destroyed %d actors."),
		Destroyed);
}

