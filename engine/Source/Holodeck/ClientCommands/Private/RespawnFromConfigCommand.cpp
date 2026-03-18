#include "RespawnFromConfigCommand.h"

#include "Holodeck.h"
#include "RuntimeRowSpawner.h"

namespace {

static bool ParseBoolLoose(const FString& Value, bool DefaultValue = false) {
	FString Lower = Value;
	Lower.TrimStartAndEndInline();
	Lower = Lower.ToLower();
	if (Lower == TEXT("1") || Lower == TEXT("true") || Lower == TEXT("yes")
		|| Lower == TEXT("y") || Lower == TEXT("on")) {
		return true;
	}
	if (Lower == TEXT("0") || Lower == TEXT("false") || Lower == TEXT("no")
		|| Lower == TEXT("n") || Lower == TEXT("off")) {
		return false;
	}
	return DefaultValue;
}

} // namespace

void URespawnFromConfigCommand::Execute() {
	UWorld* World = Target ? Target->GetWorld() : nullptr;
	if (!World) {
		UE_LOG(LogHolodeck, Error, TEXT("RespawnFromConfigCommand: world is nullptr."));
		return;
	}

	ARuntimeRowSpawner* Spawner = ARuntimeRowSpawner::FindOrCreateInWorld(World);
	if (!Spawner) {
		UE_LOG(
			LogHolodeck,
			Error,
			TEXT("RespawnFromConfigCommand: unable to get spawner."));
		return;
	}

	FString Path;
	bool	bAbsolute = false;

	if (StringParams.size() >= 1) {
		Path = FString(StringParams[0].c_str());
	}
	if (StringParams.size() >= 2) {
		bAbsolute = ParseBoolLoose(FString(StringParams[1].c_str()), false);
	}

	const bool bOk =
		Path.IsEmpty() ? Spawner->ApplyConfig() : Spawner->RespawnFromConfig(Path, bAbsolute);
	if (!bOk) {
		UE_LOG(LogHolodeck, Warning, TEXT("RespawnFromConfigCommand: apply failed."));
	}
}

