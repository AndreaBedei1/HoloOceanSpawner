// MIT License (c) 2026

#include "RuntimeRowSpawner.h"

#include "Conversion.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Holodeck.h"
#include "Internationalization/Regex.h"
#include "Json.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Octree.h"

namespace {

struct FSpawnerGlobalConfig {
	bool bDestroyExtra = false;
	bool bFailIfMeshMissing = true;
	bool bUseClientUnits = false;
	bool bVerboseLog = false;
};

struct FRowSpawnConfig {
	FString RowName;
	bool	bEnabled = true;

	FString MeshAssetPath;
	FString Folder;
	FString NamePrefix;
	FString NameRegex;
	FString TemplateActorLabel;

	TArray<FString> FilterTagsAll;
	TArray<FName>	SpawnActorTags;

	int32 TargetCount = 0;
	bool  bDestroyExtra = false;

	bool	bPositionGenerationEnabled = false;
	FVector PositionGenerationStart = FVector::ZeroVector;
	FVector PositionGenerationStep = FVector(200.f, 0.f, 0.f);
	int32	PositionGenerationCount = 0;

	TArray<FVector>	 Positions;
	TArray<FRotator> Rotations;

	FString	 OffsetSpace = TEXT("world");
	FVector	 OffsetLocation = FVector::ZeroVector;
	FRotator OffsetRotation = FRotator::ZeroRotator;

	bool  bFitScaleToReferenceActor = true;
	bool  bKeepExistingActorScaleRatio = true;
	float ExtraUniformScale = 1.f;
	FVector ExtraScaleXYZ = FVector(1.f, 1.f, 1.f);

	bool	bUseClientUnits = false;
	FString CollisionProfileOverride;
};

static FString TrimLower(FString Value) {
	Value.TrimStartAndEndInline();
	Value = Value.ToLower();
	return Value;
}

static bool ParseBoolString(const FString& Value, bool DefaultValue) {
	const FString Lower = TrimLower(Value);
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

static bool ParseVectorJson(const TSharedPtr<FJsonValue>& Value, FVector& OutVector) {
	if (!Value.IsValid()) {
		return false;
	}
	if (Value->Type == EJson::Array) {
		const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
		if (Arr.Num() >= 3) {
			OutVector.X = Arr[0]->AsNumber();
			OutVector.Y = Arr[1]->AsNumber();
			OutVector.Z = Arr[2]->AsNumber();
			return true;
		}
	}
	if (Value->Type == EJson::Object) {
		TSharedPtr<FJsonObject> Obj = Value->AsObject();
		double					 X = 0;
		double					 Y = 0;
		double					 Z = 0;
		const bool bHasX = Obj->TryGetNumberField(TEXT("x"), X)
			|| Obj->TryGetNumberField(TEXT("X"), X);
		const bool bHasY = Obj->TryGetNumberField(TEXT("y"), Y)
			|| Obj->TryGetNumberField(TEXT("Y"), Y);
		const bool bHasZ = Obj->TryGetNumberField(TEXT("z"), Z)
			|| Obj->TryGetNumberField(TEXT("Z"), Z);
		if (bHasX || bHasY || bHasZ) {
			OutVector.X = static_cast<float>(X);
			OutVector.Y = static_cast<float>(Y);
			OutVector.Z = static_cast<float>(Z);
			return true;
		}
	}
	return false;
}

static bool ParseRotatorJson(const TSharedPtr<FJsonValue>& Value, FRotator& OutRotator) {
	if (!Value.IsValid()) {
		return false;
	}
	if (Value->Type == EJson::Array) {
		const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
		if (Arr.Num() >= 3) {
			const float Roll = static_cast<float>(Arr[0]->AsNumber());
			const float Pitch = static_cast<float>(Arr[1]->AsNumber());
			const float Yaw = static_cast<float>(Arr[2]->AsNumber());
			OutRotator = RPYToRotator(Roll, Pitch, Yaw);
			return true;
		}
	}
	if (Value->Type == EJson::Object) {
		TSharedPtr<FJsonObject> Obj = Value->AsObject();
		double					 Roll = 0;
		double					 Pitch = 0;
		double					 Yaw = 0;
		const bool bHasRoll = Obj->TryGetNumberField(TEXT("roll"), Roll)
			|| Obj->TryGetNumberField(TEXT("Roll"), Roll);
		const bool bHasPitch = Obj->TryGetNumberField(TEXT("pitch"), Pitch)
			|| Obj->TryGetNumberField(TEXT("Pitch"), Pitch);
		const bool bHasYaw = Obj->TryGetNumberField(TEXT("yaw"), Yaw)
			|| Obj->TryGetNumberField(TEXT("Yaw"), Yaw);
		if (bHasRoll || bHasPitch || bHasYaw) {
			OutRotator = RPYToRotator(
				static_cast<float>(Roll),
				static_cast<float>(Pitch),
				static_cast<float>(Yaw));
			return true;
		}
	}
	return false;
}

static void ParseStringArrayField(
	const TSharedPtr<FJsonObject>& Obj,
	const FString&				   Field,
	TArray<FString>&			   OutValues) {
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Obj->TryGetArrayField(Field, Arr) || Arr == nullptr) {
		return;
	}
	for (const TSharedPtr<FJsonValue>& Val : *Arr) {
		if (Val.IsValid() && Val->Type == EJson::String) {
			OutValues.Add(Val->AsString());
		}
	}
}

static void ParseTagArrayField(
	const TSharedPtr<FJsonObject>& Obj,
	const FString&				   Field,
	TArray<FName>&				   OutTags) {
	TArray<FString> StringTags;
	ParseStringArrayField(Obj, Field, StringTags);
	for (const FString& Tag : StringTags) {
		OutTags.Add(FName(*Tag));
	}
}

static bool ParseRowsFromJson(
	const TSharedPtr<FJsonObject>& Root,
	FSpawnerGlobalConfig&		   OutGlobal,
	TArray<FRowSpawnConfig>&	   OutRows,
	bool						   DefaultFailIfMissing,
	bool						   DefaultVerbose) {
	OutGlobal.bFailIfMeshMissing = DefaultFailIfMissing;
	OutGlobal.bVerboseLog = DefaultVerbose;

	const TSharedPtr<FJsonObject>* GlobalObj = nullptr;
	if (Root->TryGetObjectField(TEXT("global"), GlobalObj) && GlobalObj != nullptr
		&& GlobalObj->IsValid()) {
		(*GlobalObj)->TryGetBoolField(TEXT("destroy_extra"), OutGlobal.bDestroyExtra);
		(*GlobalObj)->TryGetBoolField(
			TEXT("fail_if_mesh_missing"),
			OutGlobal.bFailIfMeshMissing);
		(*GlobalObj)->TryGetBoolField(TEXT("use_client_units"), OutGlobal.bUseClientUnits);
		(*GlobalObj)->TryGetBoolField(TEXT("verbose_log"), OutGlobal.bVerboseLog);
	}

	const TSharedPtr<FJsonObject>* RowsObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("rows"), RowsObj) || RowsObj == nullptr
		|| !RowsObj->IsValid()) {
		UE_LOG(LogHolodeck, Error, TEXT("RuntimeRowSpawner: Missing 'rows' JSON object."));
		return false;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& RowPair : (*RowsObj)->Values) {
		if (!RowPair.Value.IsValid() || RowPair.Value->Type != EJson::Object) {
			continue;
		}

		FRowSpawnConfig		   Row;
		TSharedPtr<FJsonObject> RowObj = RowPair.Value->AsObject();

		Row.RowName = RowPair.Key;
		Row.bDestroyExtra = OutGlobal.bDestroyExtra;
		Row.bUseClientUnits = OutGlobal.bUseClientUnits;
		RowObj->TryGetBoolField(TEXT("enabled"), Row.bEnabled);
		RowObj->TryGetStringField(TEXT("mesh_asset"), Row.MeshAssetPath);
		RowObj->TryGetStringField(TEXT("folder"), Row.Folder);
		RowObj->TryGetStringField(TEXT("name_prefix"), Row.NamePrefix);
		RowObj->TryGetStringField(TEXT("name_regex"), Row.NameRegex);
		RowObj->TryGetStringField(TEXT("template_actor_label"), Row.TemplateActorLabel);
		RowObj->TryGetStringField(
			TEXT("collision_profile"),
			Row.CollisionProfileOverride);
		RowObj->TryGetStringField(
			TEXT("collision_profile_override"),
			Row.CollisionProfileOverride);
		RowObj->TryGetBoolField(TEXT("destroy_extra"), Row.bDestroyExtra);
		RowObj->TryGetBoolField(TEXT("use_client_units"), Row.bUseClientUnits);
		RowObj->TryGetBoolField(
			TEXT("fit_scale_to_reference_actor"),
			Row.bFitScaleToReferenceActor);
		RowObj->TryGetBoolField(
			TEXT("keep_existing_actor_scale_ratio"),
			Row.bKeepExistingActorScaleRatio);
		RowObj->TryGetStringField(TEXT("offset_space"), Row.OffsetSpace);

		double Num = 0;
		if (RowObj->TryGetNumberField(TEXT("target_count"), Num)) {
			Row.TargetCount = FMath::Max(0, static_cast<int32>(Num));
		}
		if (RowObj->TryGetNumberField(TEXT("extra_uniform_scale"), Num)) {
			Row.ExtraUniformScale = static_cast<float>(Num);
		}

		const TSharedPtr<FJsonValue> OffsetLocVal = RowObj->TryGetField(TEXT("offset_location"));
		if (OffsetLocVal.IsValid()) {
			ParseVectorJson(OffsetLocVal, Row.OffsetLocation);
		}

		const TSharedPtr<FJsonValue> OffsetRotVal = RowObj->TryGetField(TEXT("offset_rotation"));
		if (OffsetRotVal.IsValid()) {
			ParseRotatorJson(OffsetRotVal, Row.OffsetRotation);
		}

		const TSharedPtr<FJsonValue> ExtraScaleVal = RowObj->TryGetField(TEXT("extra_scale_xyz"));
		if (ExtraScaleVal.IsValid()) {
			ParseVectorJson(ExtraScaleVal, Row.ExtraScaleXYZ);
		}

		ParseStringArrayField(RowObj, TEXT("filter_tags_all"), Row.FilterTagsAll);
		ParseTagArrayField(RowObj, TEXT("spawn_actor_tags"), Row.SpawnActorTags);

		const TArray<TSharedPtr<FJsonValue>>* Positions = nullptr;
		if (RowObj->TryGetArrayField(TEXT("positions"), Positions) && Positions != nullptr) {
			for (const TSharedPtr<FJsonValue>& Position : *Positions) {
				FVector Parsed;
				if (ParseVectorJson(Position, Parsed)) {
					Row.Positions.Add(Parsed);
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Rotations = nullptr;
		if (RowObj->TryGetArrayField(TEXT("rotations"), Rotations) && Rotations != nullptr) {
			for (const TSharedPtr<FJsonValue>& Rotation : *Rotations) {
				FRotator Parsed;
				if (ParseRotatorJson(Rotation, Parsed)) {
					Row.Rotations.Add(Parsed);
				}
			}
		}

		const TSharedPtr<FJsonObject>* PositionGenerationObj = nullptr;
		if (RowObj->TryGetObjectField(TEXT("position_generation"), PositionGenerationObj)
			&& PositionGenerationObj != nullptr && PositionGenerationObj->IsValid()) {
			(*PositionGenerationObj)->TryGetBoolField(
				TEXT("enabled"),
				Row.bPositionGenerationEnabled);

			double GenCount = 0;
			if ((*PositionGenerationObj)->TryGetNumberField(TEXT("count"), GenCount)) {
				Row.PositionGenerationCount =
					FMath::Max(0, static_cast<int32>(GenCount));
			}

			const TSharedPtr<FJsonValue> StartVal =
				(*PositionGenerationObj)->TryGetField(TEXT("start"));
			if (StartVal.IsValid()) {
				ParseVectorJson(StartVal, Row.PositionGenerationStart);
			}

			const TSharedPtr<FJsonValue> StepVal =
				(*PositionGenerationObj)->TryGetField(TEXT("step"));
			if (StepVal.IsValid()) {
				ParseVectorJson(StepVal, Row.PositionGenerationStep);
			}
		}

		OutRows.Add(Row);
	}

	return OutRows.Num() > 0;
}

static FString GetActorMatchName(const AActor* Actor) {
#if WITH_EDITOR
	return Actor->GetActorLabel();
#else
	return Actor->GetName();
#endif
}

static bool MatchesNamePrefix(const AActor* Actor, const FString& Prefix) {
	if (Prefix.IsEmpty()) {
		return true;
	}
	if (Actor->GetName().StartsWith(Prefix, ESearchCase::IgnoreCase)) {
		return true;
	}
#if WITH_EDITOR
	return Actor->GetActorLabel().StartsWith(Prefix, ESearchCase::IgnoreCase);
#else
	return false;
#endif
}

static bool MatchesNameRegex(const AActor* Actor, const FString& RegexPattern) {
	if (RegexPattern.IsEmpty()) {
		return true;
	}
	const FRegexPattern Pattern(RegexPattern);
	FRegexMatcher		NameMatcher(Pattern, Actor->GetName());
	if (NameMatcher.FindNext()) {
		return true;
	}
#if WITH_EDITOR
	FRegexMatcher LabelMatcher(Pattern, Actor->GetActorLabel());
	if (LabelMatcher.FindNext()) {
		return true;
	}
#endif
	return false;
}

static bool MatchesRequiredTags(const AActor* Actor, const TArray<FString>& RequiredTags) {
	for (const FString& RequiredTag : RequiredTags) {
		if (!Actor->Tags.Contains(FName(*RequiredTag))) {
			return false;
		}
	}
	return true;
}

static bool MatchesFolder(const AActor* Actor, const FString& FolderFilter) {
	if (FolderFilter.IsEmpty()) {
		return true;
	}
#if WITH_EDITOR
	const FString FolderPath = Actor->GetFolderPath().ToString();
	return FolderPath.Contains(FolderFilter, ESearchCase::IgnoreCase);
#else
	return true;
#endif
}

static void ParseNameSuffix(const FString& Input, FString& OutStem, int32& OutNumber) {
	int32 Index = Input.Len() - 1;
	while (Index >= 0 && FChar::IsDigit(Input[Index])) {
		Index--;
	}
	OutStem = Input.Left(Index + 1).ToLower();
	const FString NumberText = Input.Mid(Index + 1);
	OutNumber = NumberText.IsEmpty() ? MAX_int32 : FCString::Atoi(*NumberText);
}

static TArray<AStaticMeshActor*>
FindMatchingActors(UWorld* World, const FRowSpawnConfig& RowConfig) {
	TArray<AStaticMeshActor*> Matches;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It) {
		AStaticMeshActor* Actor = *It;
		if (!IsValid(Actor)) {
			continue;
		}
		if (!MatchesNamePrefix(Actor, RowConfig.NamePrefix)) {
			continue;
		}
		if (!MatchesNameRegex(Actor, RowConfig.NameRegex)) {
			continue;
		}
		if (!MatchesRequiredTags(Actor, RowConfig.FilterTagsAll)) {
			continue;
		}
		if (!MatchesFolder(Actor, RowConfig.Folder)) {
			continue;
		}
		Matches.Add(Actor);
	}

	Matches.Sort([](const AStaticMeshActor& ARef, const AStaticMeshActor& BRef) {
		const FString NameA = GetActorMatchName(&ARef);
		const FString NameB = GetActorMatchName(&BRef);
		FString		  StemA;
		FString		  StemB;
		int32		  NumA = MAX_int32;
		int32		  NumB = MAX_int32;
		ParseNameSuffix(NameA, StemA, NumA);
		ParseNameSuffix(NameB, StemB, NumB);

		if (StemA != StemB) {
			return StemA < StemB;
		}
		if (NumA != NumB) {
			return NumA < NumB;
		}
		return NameA < NameB;
	});

	return Matches;
}

static AStaticMeshActor* FindTemplateActor(
	const TArray<AStaticMeshActor*>& Actors,
	const FString&					TemplateLabel) {
	if (Actors.Num() == 0) {
		return nullptr;
	}
	if (!TemplateLabel.IsEmpty()) {
		for (AStaticMeshActor* Actor : Actors) {
			if (!IsValid(Actor)) {
				continue;
			}
			if (Actor->GetName().Equals(TemplateLabel, ESearchCase::IgnoreCase)) {
				return Actor;
			}
#if WITH_EDITOR
			if (Actor->GetActorLabel().Equals(TemplateLabel, ESearchCase::IgnoreCase)) {
				return Actor;
			}
#endif
		}
	}
	return Actors[0];
}

static UStaticMesh* LoadStaticMeshByPath(const FString& MeshAssetPath) {
	if (MeshAssetPath.IsEmpty()) {
		return nullptr;
	}
	return LoadObject<UStaticMesh>(nullptr, *MeshAssetPath);
}

static FVector GetMeshSize(const UStaticMesh* Mesh) {
	if (!IsValid(Mesh)) {
		return FVector::ZeroVector;
	}
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	return Bounds.BoxExtent * 2.f;
}

static float ComputeUniformFitScale(
	const AStaticMeshActor* TemplateActor,
	const UStaticMesh*		 TargetMesh,
	bool					 bEnabled) {
	if (!bEnabled || !IsValid(TemplateActor) || !IsValid(TargetMesh)) {
		return 1.f;
	}
	const UStaticMeshComponent* TemplateSM = TemplateActor->GetStaticMeshComponent();
	if (!IsValid(TemplateSM) || !IsValid(TemplateSM->GetStaticMesh())) {
		return 1.f;
	}

	const FVector Src = GetMeshSize(TemplateSM->GetStaticMesh());
	const FVector Dst = GetMeshSize(TargetMesh);
	const float	  SrcMax = FMath::Max3(FMath::Abs(Src.X), FMath::Abs(Src.Y), FMath::Abs(Src.Z));
	const float	  DstMax = FMath::Max3(FMath::Abs(Dst.X), FMath::Abs(Dst.Y), FMath::Abs(Dst.Z));
	if (DstMax <= KINDA_SMALL_NUMBER) {
		return 1.f;
	}
	return SrcMax / DstMax;
}

static void CopyTemplateSettings(const AStaticMeshActor* TemplateActor, AStaticMeshActor* TargetActor) {
	if (!IsValid(TemplateActor) || !IsValid(TargetActor)) {
		return;
	}
	TargetActor->Tags = TemplateActor->Tags;

	UStaticMeshComponent* TargetSM = TargetActor->GetStaticMeshComponent();
	UStaticMeshComponent* SourceSM = TemplateActor->GetStaticMeshComponent();
	if (!IsValid(SourceSM) || !IsValid(TargetSM)) {
		return;
	}

	TargetSM->SetMobility(SourceSM->Mobility);
	TargetSM->SetCollisionEnabled(SourceSM->GetCollisionEnabled());
	TargetSM->SetCollisionObjectType(SourceSM->GetCollisionObjectType());
	TargetSM->SetCollisionProfileName(SourceSM->GetCollisionProfileName());
	TargetSM->SetGenerateOverlapEvents(SourceSM->GetGenerateOverlapEvents());
	TargetSM->SetCanEverAffectNavigation(SourceSM->CanEverAffectNavigation());
	TargetSM->SetVisibility(SourceSM->IsVisible(), true);
	TargetSM->SetHiddenInGame(SourceSM->bHiddenInGame);
	TargetSM->SetCastShadow(SourceSM->CastShadow);
	TargetSM->SetRenderCustomDepth(SourceSM->bRenderCustomDepth);
	TargetSM->SetCustomDepthStencilValue(SourceSM->CustomDepthStencilValue);
	TargetSM->ComponentTags = SourceSM->ComponentTags;
	TargetSM->SetWorldScale3D(SourceSM->GetComponentScale());
}

static AStaticMeshActor* SpawnStaticMeshActor(UWorld* World, const FVector& Loc, const FRotator& Rot) {
	if (!IsValid(World)) {
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Loc, Rot, Params);
}

static bool AssignStaticMeshRuntime(UStaticMeshComponent* SM, UStaticMesh* Mesh) {
	if (!IsValid(SM) || !IsValid(Mesh)) {
		return false;
	}

	const EComponentMobility::Type OriginalMobility = SM->Mobility;
	if (OriginalMobility == EComponentMobility::Static) {
		SM->SetMobility(EComponentMobility::Movable);
	}

	const bool bSetOk = SM->SetStaticMesh(Mesh);
	SM->SetVisibility(true, true);
	SM->SetHiddenInGame(false, true);
	SM->MarkRenderStateDirty();
	SM->RecreatePhysicsState();

	if (OriginalMobility == EComponentMobility::Static) {
		SM->SetMobility(EComponentMobility::Static);
	}
	return bSetOk;
}

static void BuildPositions(const FRowSpawnConfig& RowConfig, const TArray<AStaticMeshActor*>& Existing, TArray<FVector>& OutPositions) {
	if (RowConfig.Positions.Num() > 0) {
		OutPositions = RowConfig.Positions;
		return;
	}
	if (RowConfig.bPositionGenerationEnabled && RowConfig.PositionGenerationCount > 0) {
		for (int32 Index = 0; Index < RowConfig.PositionGenerationCount; Index++) {
			OutPositions.Add(
				RowConfig.PositionGenerationStart
				+ RowConfig.PositionGenerationStep * static_cast<float>(Index));
		}
		return;
	}
	for (AStaticMeshActor* Actor : Existing) {
		if (IsValid(Actor)) {
			OutPositions.Add(Actor->GetActorLocation());
		}
	}
}

static void ExpandPositionsToCount(
	TArray<FVector>& Positions,
	int32			 TargetCount,
	const FVector&	 DefaultStep) {
	if (TargetCount <= 0) {
		return;
	}
	if (Positions.Num() == 0) {
		Positions.Add(FVector::ZeroVector);
	}
	FVector Step = DefaultStep;
	if (Positions.Num() >= 2) {
		Step = Positions[Positions.Num() - 1] - Positions[Positions.Num() - 2];
	}
	while (Positions.Num() < TargetCount) {
		Positions.Add(Positions.Last() + Step);
	}
	if (Positions.Num() > TargetCount) {
		Positions.SetNum(TargetCount);
	}
}

static void BuildRotations(
	const FRowSpawnConfig&		RowConfig,
	const TArray<AStaticMeshActor*>& Existing,
	int32							TargetCount,
	TArray<FRotator>&					OutRotations) {
	if (RowConfig.Rotations.Num() > 0) {
		OutRotations = RowConfig.Rotations;
	} else {
		for (AStaticMeshActor* Actor : Existing) {
			if (IsValid(Actor)) {
				OutRotations.Add(Actor->GetActorRotation());
			}
		}
	}
	if (OutRotations.Num() == 0) {
		OutRotations.Add(FRotator::ZeroRotator);
	}
	while (OutRotations.Num() < TargetCount) {
		OutRotations.Add(OutRotations.Last());
	}
	if (OutRotations.Num() > TargetCount) {
		OutRotations.SetNum(TargetCount);
	}
}

static FVector MaybeConvertLocation(const FVector& Value, bool bUseClientUnits) {
	return bUseClientUnits ? ConvertLinearVector(Value, ClientToUE) : Value;
}

static bool ApplyOneRow(
	ARuntimeRowSpawner*	 Owner,
	UWorld*				 World,
	const FRowSpawnConfig& RowConfig,
	const FSpawnerGlobalConfig& GlobalConfig) {
	TArray<AStaticMeshActor*> Existing = FindMatchingActors(World, RowConfig);
	AStaticMeshActor*		 TemplateActor =
		FindTemplateActor(Existing, RowConfig.TemplateActorLabel);

	UStaticMesh* NewMesh = LoadStaticMeshByPath(RowConfig.MeshAssetPath);
	if (!IsValid(NewMesh) && !RowConfig.MeshAssetPath.IsEmpty()) {
		UE_LOG(
			LogHolodeck,
			Warning,
			TEXT("RuntimeRowSpawner: [%s] mesh not found: %s"),
			*RowConfig.RowName,
			*RowConfig.MeshAssetPath);
		if (GlobalConfig.bFailIfMeshMissing) {
			return false;
		}
	}

	TArray<FVector> Positions;
	BuildPositions(RowConfig, Existing, Positions);

	if (RowConfig.TargetCount > 0) {
		ExpandPositionsToCount(
			Positions,
			RowConfig.TargetCount,
			RowConfig.PositionGenerationStep);
	}
	const int32 TargetCount = Positions.Num();
	if (TargetCount <= 0) {
		UE_LOG(
			LogHolodeck,
			Warning,
			TEXT("RuntimeRowSpawner: [%s] target_count resolved to 0."),
			*RowConfig.RowName);
		return true;
	}

	TArray<FRotator> Rotations;
	BuildRotations(RowConfig, Existing, TargetCount, Rotations);

	TArray<AStaticMeshActor*> Actors = Existing;
	while (Actors.Num() < TargetCount) {
		const int32	   NewIdx = Actors.Num();
		const FVector  SpawnLoc = MaybeConvertLocation(Positions[NewIdx], RowConfig.bUseClientUnits);
		const FRotator SpawnRot = Rotations[NewIdx];
		AStaticMeshActor* Spawned = SpawnStaticMeshActor(World, SpawnLoc, SpawnRot);
		if (!IsValid(Spawned)) {
			UE_LOG(
				LogHolodeck,
				Error,
				TEXT("RuntimeRowSpawner: [%s] failed to spawn actor index %d."),
				*RowConfig.RowName,
				NewIdx);
			return false;
		}
		if (IsValid(TemplateActor)) {
			CopyTemplateSettings(TemplateActor, Spawned);
		} else {
			UStaticMeshComponent* SM = Spawned->GetStaticMeshComponent();
			if (IsValid(SM)) {
				SM->SetMobility(EComponentMobility::Static);
				SM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				SM->SetCollisionProfileName(TEXT("BlockAll"));
			}
		}
		Owner->RegisterSpawnedActor(Spawned);
		Actors.Add(Spawned);
	}

	if (Actors.Num() > TargetCount && RowConfig.bDestroyExtra) {
		for (int32 Index = TargetCount; Index < Actors.Num(); Index++) {
			if (IsValid(Actors[Index])) {
				Actors[Index]->Destroy();
			}
		}
		Actors.SetNum(TargetCount);
	}

	const float UniformFit = ComputeUniformFitScale(
		TemplateActor,
		NewMesh,
		RowConfig.bFitScaleToReferenceActor);

	for (int32 Index = 0; Index < FMath::Min(TargetCount, Actors.Num()); Index++) {
		AStaticMeshActor* Actor = Actors[Index];
		if (!IsValid(Actor)) {
			continue;
		}

		if (IsValid(TemplateActor) && Actor != TemplateActor) {
			CopyTemplateSettings(TemplateActor, Actor);
		}

		UStaticMeshComponent* SM = Actor->GetStaticMeshComponent();
		if (IsValid(SM) && IsValid(NewMesh)) {
			if (!AssignStaticMeshRuntime(SM, NewMesh)) {
				UE_LOG(
					LogHolodeck,
					Warning,
					TEXT("RuntimeRowSpawner: [%s] SetStaticMesh failed for actor '%s'."),
					*RowConfig.RowName,
					*Actor->GetName());
			}
		}
		if (IsValid(SM) && !RowConfig.CollisionProfileOverride.IsEmpty()) {
			SM->SetCollisionProfileName(*RowConfig.CollisionProfileOverride);
		}

		if (RowConfig.SpawnActorTags.Num() > 0) {
			Actor->Tags = RowConfig.SpawnActorTags;
		}

		const FVector BaseLocation = MaybeConvertLocation(Positions[Index], RowConfig.bUseClientUnits);
		const FRotator BaseRotation = Rotations[Index];
		const FVector Offset = MaybeConvertLocation(RowConfig.OffsetLocation, RowConfig.bUseClientUnits);

		const bool bLocalOffset = TrimLower(RowConfig.OffsetSpace) == TEXT("local");
		const FVector FinalLocation =
			BaseLocation + (bLocalOffset ? BaseRotation.RotateVector(Offset) : Offset);
		const FRotator FinalRotation = BaseRotation + RowConfig.OffsetRotation;

		Actor->SetActorLocation(FinalLocation, false, nullptr, ETeleportType::TeleportPhysics);
		Actor->SetActorRotation(FinalRotation, ETeleportType::TeleportPhysics);

		FVector BaseScale = Actor->GetActorScale3D();
		if (!RowConfig.bKeepExistingActorScaleRatio && IsValid(TemplateActor)) {
			BaseScale = TemplateActor->GetActorScale3D();
		}
		const float Uniform = UniformFit * RowConfig.ExtraUniformScale;
		const FVector FinalScale = FVector(
			BaseScale.X * Uniform * RowConfig.ExtraScaleXYZ.X,
			BaseScale.Y * Uniform * RowConfig.ExtraScaleXYZ.Y,
			BaseScale.Z * Uniform * RowConfig.ExtraScaleXYZ.Z);
		Actor->SetActorScale3D(FinalScale);
		Actor->SetActorHiddenInGame(false);
		Actor->SetActorEnableCollision(true);

#if WITH_EDITOR
		if (!RowConfig.NamePrefix.IsEmpty()) {
			Actor->SetActorLabel(
				FString::Printf(TEXT("%s%d"), *RowConfig.NamePrefix, Index + 1));
		}
		if (!RowConfig.Folder.IsEmpty()) {
			Actor->SetFolderPath(FName(*RowConfig.Folder));
		}
#endif
	}

	if (GlobalConfig.bVerboseLog) {
		UE_LOG(
			LogHolodeck,
			Log,
			TEXT("RuntimeRowSpawner: [%s] applied. matched=%d target=%d"),
			*RowConfig.RowName,
			Existing.Num(),
			TargetCount);
	}
	return true;
}

} // namespace

ARuntimeRowSpawner::ARuntimeRowSpawner() {
	PrimaryActorTick.bCanEverTick = false;
}

void ARuntimeRowSpawner::BeginPlay() {
	Super::BeginPlay();
	if (bApplyOnBeginPlay) {
		ApplyConfig();
	}
}

void ARuntimeRowSpawner::ApplyConfigInEditor() {
	ApplyConfig();
}

bool ARuntimeRowSpawner::ApplyConfig() {
	return ApplyConfigInternal(ConfigPath, bConfigPathIsAbsolute);
}

bool ARuntimeRowSpawner::SpawnAsset(
	const FString&	MeshAssetPath,
	const FVector&	Location,
	const FRotator& Rotation,
	const FVector&	Scale,
	const FString&	SpawnLabel,
	bool			bUseClientUnits) {
	UWorld* World = GetWorld();
	if (!IsValid(World)) {
		UE_LOG(LogHolodeck, Error, TEXT("RuntimeRowSpawner: invalid world."));
		return false;
	}

	UStaticMesh* Mesh = LoadStaticMeshByPath(MeshAssetPath);
	if (!IsValid(Mesh)) {
		UE_LOG(
			LogHolodeck,
			Error,
			TEXT("RuntimeRowSpawner: cannot load mesh '%s'."),
			*MeshAssetPath);
		return false;
	}

	const FVector SpawnLoc = MaybeConvertLocation(Location, bUseClientUnits);
	AStaticMeshActor* Spawned = SpawnStaticMeshActor(World, SpawnLoc, Rotation);
	if (!IsValid(Spawned)) {
		UE_LOG(LogHolodeck, Error, TEXT("RuntimeRowSpawner: failed to spawn actor."));
		return false;
	}

	UStaticMeshComponent* SM = Spawned->GetStaticMeshComponent();
	if (IsValid(SM)) {
		if (!AssignStaticMeshRuntime(SM, Mesh)) {
			UE_LOG(
				LogHolodeck,
				Warning,
				TEXT("RuntimeRowSpawner: SetStaticMesh failed for '%s'."),
				*MeshAssetPath);
		}
		SM->SetMobility(EComponentMobility::Static);
		SM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		SM->SetCollisionProfileName(TEXT("BlockAll"));
	}
	Spawned->SetActorScale3D(Scale);
	Spawned->SetActorHiddenInGame(false);
	Spawned->SetActorEnableCollision(true);

#if WITH_EDITOR
	if (!SpawnLabel.IsEmpty()) {
		Spawned->SetActorLabel(SpawnLabel);
	}
#endif

	RegisterSpawnedActor(Spawned);
	Octree::MarkWorldGeometryDirty();
	return true;
}

int32 ARuntimeRowSpawner::ClearSpawned() {
	CleanupSpawnedList();
	int32 Destroyed = 0;
	for (int32 Index = SpawnedActors.Num() - 1; Index >= 0; Index--) {
		AActor* Actor = SpawnedActors[Index];
		if (IsValid(Actor)) {
			Actor->Destroy();
			Destroyed++;
		}
	}
	SpawnedActors.Reset();
	if (Destroyed > 0) {
		Octree::MarkWorldGeometryDirty();
	}
	if (bVerboseLog) {
		UE_LOG(LogHolodeck, Log, TEXT("RuntimeRowSpawner: cleared %d spawned actors."), Destroyed);
	}
	return Destroyed;
}

bool ARuntimeRowSpawner::RespawnFromConfig(const FString& InConfigPath, bool bPathIsAbsolute) {
	ClearSpawned();
	const FString EffectivePath = InConfigPath.IsEmpty() ? ConfigPath : InConfigPath;
	const bool	  EffectiveAbs = InConfigPath.IsEmpty() ? bConfigPathIsAbsolute : bPathIsAbsolute;
	return ApplyConfigInternal(EffectivePath, EffectiveAbs);
}

ARuntimeRowSpawner* ARuntimeRowSpawner::FindInWorld(UWorld* World) {
	if (!IsValid(World)) {
		return nullptr;
	}
	for (TActorIterator<ARuntimeRowSpawner> It(World); It; ++It) {
		if (IsValid(*It)) {
			return *It;
		}
	}
	return nullptr;
}

ARuntimeRowSpawner* ARuntimeRowSpawner::FindOrCreateInWorld(UWorld* World) {
	if (!IsValid(World)) {
		return nullptr;
	}
	if (ARuntimeRowSpawner* Existing = FindInWorld(World)) {
		return Existing;
	}

	FActorSpawnParameters Params;
	Params.Name = TEXT("RuntimeRowSpawner_Auto");
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ARuntimeRowSpawner* Spawned =
		World->SpawnActor<ARuntimeRowSpawner>(ARuntimeRowSpawner::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (IsValid(Spawned)) {
		Spawned->bApplyOnBeginPlay = false;
		Spawned->SetActorHiddenInGame(true);
		Spawned->SetActorEnableCollision(false);
	}
	return Spawned;
}

bool ARuntimeRowSpawner::ApplyConfigInternal(const FString& InConfigPath, bool bPathIsAbsolute) {
	UWorld* World = GetWorld();
	if (!IsValid(World)) {
		UE_LOG(LogHolodeck, Error, TEXT("RuntimeRowSpawner: invalid world."));
		return false;
	}

	FString AbsoluteConfigPath;
	if (!ResolveConfigPath(InConfigPath, bPathIsAbsolute, AbsoluteConfigPath)) {
		UE_LOG(
			LogHolodeck,
			Error,
			TEXT("RuntimeRowSpawner: config file not found: %s"),
			*InConfigPath);
		return false;
	}

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *AbsoluteConfigPath)) {
		UE_LOG(
			LogHolodeck,
			Error,
			TEXT("RuntimeRowSpawner: failed reading config file: %s"),
			*AbsoluteConfigPath);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) {
		UE_LOG(
			LogHolodeck,
			Error,
			TEXT("RuntimeRowSpawner: invalid JSON config file: %s"),
			*AbsoluteConfigPath);
		return false;
	}

	FSpawnerGlobalConfig   GlobalConfig;
	TArray<FRowSpawnConfig> Rows;
	if (!ParseRowsFromJson(Root, GlobalConfig, Rows, bFailIfMeshMissing, bVerboseLog)) {
		return false;
	}

	bool bAllRowsSucceeded = true;
	bool bAnyEnabledRowApplied = false;
	for (const FRowSpawnConfig& Row : Rows) {
		if (!Row.bEnabled) {
			continue;
		}
		bAnyEnabledRowApplied = true;
		const bool bRowSuccess = ApplyOneRow(this, World, Row, GlobalConfig);
		bAllRowsSucceeded = bAllRowsSucceeded && bRowSuccess;
	}

	if (bAnyEnabledRowApplied) {
		Octree::MarkWorldGeometryDirty();
	}

	return bAllRowsSucceeded;
}

bool ARuntimeRowSpawner::ResolveConfigPath(
	const FString& InConfigPath,
	bool		   bPathIsAbsolute,
	FString&	   OutAbsolutePath) const {
	if (InConfigPath.IsEmpty()) {
		return false;
	}

	if (bPathIsAbsolute || FPaths::IsRelative(InConfigPath) == false) {
		const FString Candidate = FPaths::ConvertRelativePathToFull(InConfigPath);
		if (IFileManager::Get().FileExists(*Candidate)) {
			OutAbsolutePath = Candidate;
			return true;
		}
		return false;
	}

	TArray<FString> Candidates;
	Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), InConfigPath)));
	Candidates.Add(
		FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), InConfigPath)));
	Candidates.Add(
		FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectConfigDir(), InConfigPath)));
	Candidates.Add(
		FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), InConfigPath)));
	Candidates.Add(FPaths::ConvertRelativePathToFull(InConfigPath));

	for (const FString& Candidate : Candidates) {
		if (IFileManager::Get().FileExists(*Candidate)) {
			OutAbsolutePath = Candidate;
			return true;
		}
	}
	return false;
}

void ARuntimeRowSpawner::CleanupSpawnedList() {
	for (int32 Index = SpawnedActors.Num() - 1; Index >= 0; Index--) {
		if (!IsValid(SpawnedActors[Index])) {
			SpawnedActors.RemoveAtSwap(Index);
		}
	}
}

void ARuntimeRowSpawner::RegisterSpawnedActor(AActor* Actor) {
	if (!IsValid(Actor)) {
		return;
	}
	SpawnedActors.Add(Actor);
}
