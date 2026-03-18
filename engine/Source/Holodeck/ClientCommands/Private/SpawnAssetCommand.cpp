#include "SpawnAssetCommand.h"

#include "Conversion.h"
#include "Holodeck.h"
#include "RuntimeRowSpawner.h"

void USpawnAssetCommand::Execute() {
	if (NumberParams.size() != 9 || StringParams.size() < 1) {
		UE_LOG(
			LogHolodeck,
			Error,
			TEXT(
				"SpawnAssetCommand bad args. Expected 9 numeric params and at "
				"least 1 string param (mesh path)."));
		return;
	}

	UWorld* World = Target ? Target->GetWorld() : nullptr;
	if (!World) {
		UE_LOG(LogHolodeck, Error, TEXT("SpawnAssetCommand: world is nullptr."));
		return;
	}

	ARuntimeRowSpawner* Spawner = ARuntimeRowSpawner::FindOrCreateInWorld(World);
	if (!Spawner) {
		UE_LOG(LogHolodeck, Error, TEXT("SpawnAssetCommand: unable to get spawner."));
		return;
	}

	const FString MeshPath(StringParams[0].c_str());
	const FString ActorLabel =
		StringParams.size() >= 2 ? FString(StringParams[1].c_str()) : FString();

	bool bUseClientUnits = false;
	if (StringParams.size() >= 3) {
		FString Units(StringParams[2].c_str());
		Units = Units.ToLower();
		Units.TrimStartAndEndInline();
		bUseClientUnits = (Units == TEXT("m") || Units == TEXT("meter")
			|| Units == TEXT("meters") || Units == TEXT("client"));
	}

	const FVector Location(NumberParams[0], NumberParams[1], NumberParams[2]);
	const FRotator Rotation =
		RPYToRotator(NumberParams[3], NumberParams[4], NumberParams[5]);
	const FVector Scale(NumberParams[6], NumberParams[7], NumberParams[8]);

	const bool bOk = Spawner->SpawnAsset(
		MeshPath,
		Location,
		Rotation,
		Scale,
		ActorLabel,
		bUseClientUnits);
	if (!bOk) {
		UE_LOG(LogHolodeck, Warning, TEXT("SpawnAssetCommand: spawn failed."));
	}
}
