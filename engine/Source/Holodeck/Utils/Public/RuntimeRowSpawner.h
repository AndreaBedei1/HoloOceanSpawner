// MIT License (c) 2026

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RuntimeRowSpawner.generated.h"

class UStaticMesh;

/**
 * Runtime spawner used by world commands:
 * - SpawnAsset
 * - ClearSpawned
 * - RespawnFromConfig
 */
UCLASS(BlueprintType, Blueprintable)
class HOLODECK_API ARuntimeRowSpawner : public AActor {
	GENERATED_BODY()

public:
	ARuntimeRowSpawner();

	virtual void BeginPlay() override;

	/** Loads JSON config and applies spawn/replace logic. */
	UFUNCTION(BlueprintCallable, Category = "HoloOcean|RuntimeSpawner")
	bool ApplyConfig();

	/** Utility button for testing in editor. */
	UFUNCTION(CallInEditor, Category = "HoloOcean|RuntimeSpawner")
	void ApplyConfigInEditor();

	/** Spawns one static mesh actor and registers it in the spawned list. */
	UFUNCTION(BlueprintCallable, Category = "HoloOcean|RuntimeSpawner")
	bool SpawnAsset(
		const FString&  MeshAssetPath,
		const FVector&  Location,
		const FRotator& Rotation,
		const FVector&  Scale,
		const FString&  SpawnLabel = TEXT(""),
		bool			bUseClientUnits = false);

	/** Destroys actors spawned via this spawner. Returns number destroyed. */
	UFUNCTION(BlueprintCallable, Category = "HoloOcean|RuntimeSpawner")
	int32 ClearSpawned();

	/** Clears tracked spawned actors and applies config from path. */
	UFUNCTION(BlueprintCallable, Category = "HoloOcean|RuntimeSpawner")
	bool RespawnFromConfig(const FString& InConfigPath, bool bPathIsAbsolute = false);

	/** Finds first spawner in world. */
	static ARuntimeRowSpawner* FindInWorld(UWorld* World);

	/** Finds or auto-creates one spawner. */
	static ARuntimeRowSpawner* FindOrCreateInWorld(UWorld* World);

protected:
	/** If true, calls ApplyConfig() during BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runtime Config")
	bool bApplyOnBeginPlay = false;

	/**
	 * Absolute or relative path to JSON config.
	 * Relative path search order:
	 * - ProjectDir
	 * - ProjectContentDir
	 * - ProjectConfigDir
	 * - ProjectSavedDir
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runtime Config")
	FString ConfigPath = TEXT("Saved/sonar_rows_runtime.json");

	/** If true, ConfigPath is treated as absolute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runtime Config")
	bool bConfigPathIsAbsolute = false;

	/** If true, ApplyConfig fails when a mesh path in JSON cannot be loaded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runtime Config")
	bool bFailIfMeshMissing = true;

	/** Optional references to force cooking of dynamically loaded meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runtime Config")
	TArray<TSoftObjectPtr<UStaticMesh>> AlwaysCookMeshes;

	/** Extra logs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runtime Config")
	bool bVerboseLog = true;

private:
	bool ApplyConfigInternal(const FString& InConfigPath, bool bPathIsAbsolute);
	bool ResolveConfigPath(
		const FString& InConfigPath,
		bool		   bPathIsAbsolute,
		FString&	   OutAbsolutePath) const;

public:
	void CleanupSpawnedList();
	void RegisterSpawnedActor(AActor* Actor);

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnedActors;
};
