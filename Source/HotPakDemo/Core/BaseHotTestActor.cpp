// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseHotTestActor.h"

#include "HotUpdateSubsystem.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreaming.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "../HotPakDemo.h"
#include "Kismet/GameplayStatics.h"

ABaseHotTestActor::ABaseHotTestActor()
{
	PrimaryActorTick.bCanEverTick = false;
	PatchActorSpawnTransform = FTransform::Identity;
}

void ABaseHotTestActor::BeginPlay()
{
	Super::BeginPlay();

	RegisterHotfixCompleteCallback();
	UE_LOG(LogHotUpdate, Log, TEXT("%s: hot-update startup state, version=%s, patch_mounted=%s"),
		*GetName(), *GetCurrentVersionString(), IsPatchMounted() ? TEXT("true") : TEXT("false"));

	// A patch can already be mounted before the test actor enters the level.
	// Let the Blueprint decide how to apply those resources as well.
	if (IsPatchMounted())
	{
		OnHotfixApplied();
	}
}

void ABaseHotTestActor::RegisterHotfixCompleteCallback()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		HotUpdateSubsystem = GameInstance->GetSubsystem<UHotUpdateSubsystem>();
		if (UHotUpdateSubsystem* Subsystem = HotUpdateSubsystem.Get())
		{
			Subsystem->OnHotfixApplied.AddDynamic(this, &ABaseHotTestActor::HandleHotfixApplied);
			return;
		}
	}

	UE_LOG(LogHotUpdate, Warning, TEXT("%s: UHotUpdateSubsystem is unavailable; using fallback hot-update state."), *GetName());
}

void ABaseHotTestActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UHotUpdateSubsystem* Subsystem = HotUpdateSubsystem.Get())
	{
		Subsystem->OnHotfixApplied.RemoveDynamic(this, &ABaseHotTestActor::HandleHotfixApplied);
	}

	Super::EndPlay(EndPlayReason);
}

void ABaseHotTestActor::HandleHotfixApplied(const FString& NewVersion)
{
	UE_LOG(LogHotUpdate, Log, TEXT("%s: hotfix applied, version=%s"), *GetName(), *NewVersion);
	OnHotfixApplied();
}

FString ABaseHotTestActor::GetCurrentVersionString() const
{
	if (const UHotUpdateSubsystem* Subsystem = HotUpdateSubsystem.Get())
	{
		return Subsystem->GetCurrentVersionString();
	}

	return FallbackVersionString;
}

bool ABaseHotTestActor::IsPatchMounted() const
{
	return HotUpdateSubsystem.IsValid() && HotUpdateSubsystem->IsPatchMounted();
}

void ABaseHotTestActor::ForceReloadAllSlots()
{
	if (!HotfixTexture.IsNull())
	{
		UpdateTexture(HotfixTexture.LoadSynchronous());
	}
	else
	{
		UE_LOG(LogHotUpdate, Verbose, TEXT("%s: texture slot is not configured."), *GetName());
	}

	if (!HotfixDataTable.IsNull())
	{
		UpdateDataTable(HotfixDataTable.LoadSynchronous());
	}
	else
	{
		UE_LOG(LogHotUpdate, Verbose, TEXT("%s: data table slot is not configured."), *GetName());
	}

	if (!HotfixActorClass.IsNull())
	{
		SpawnPatchActor(HotfixActorClass.LoadSynchronous());
	}
	else
	{
		UE_LOG(LogHotUpdate, Verbose, TEXT("%s: patch actor slot is not configured."), *GetName());
	}

	if (!HotfixStreamLevel.IsNone())
	{
		LoadStreamLevel(HotfixStreamLevel);
	}
	else
	{
		UE_LOG(LogHotUpdate, Verbose, TEXT("%s: streaming level slot is not configured."), *GetName());
	}
}

bool ABaseHotTestActor::IsStreamingLevelLoaded(FName LevelPackageName) const
{
	if (LevelPackageName.IsNone())
	{
		return false;
	}

	const ULevelStreaming* StreamingLevel = UGameplayStatics::GetStreamingLevel(this, LevelPackageName);
	return IsValid(StreamingLevel) && StreamingLevel->IsLevelLoaded();
}

void ABaseHotTestActor::UpdateTexture_Implementation(UTexture2D* NewTex)
{
	UE_LOG(LogHotUpdate, Log, TEXT("%s: texture slot received %s. Override UpdateTexture in Blueprint to apply it."),
		*GetName(), NewTex ? *NewTex->GetName() : TEXT("<null>"));
}

void ABaseHotTestActor::UpdateDataTable_Implementation(UDataTable* NewDT)
{
	UE_LOG(LogHotUpdate, Log, TEXT("%s: data table slot received %s. Override UpdateDataTable in Blueprint to apply it."),
		*GetName(), NewDT ? *NewDT->GetName() : TEXT("<null>"));
}

void ABaseHotTestActor::SpawnPatchActor_Implementation(TSubclassOf<AActor> NewBPActorClass)
{
	if (!NewBPActorClass)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("%s: cannot spawn an empty patch actor class."), *GetName());
		return;
	}

	if (IsValid(SpawnedPatchActor))
	{
		SpawnedPatchActor->Destroy();
		SpawnedPatchActor = nullptr;
	}

	if (UWorld* World = GetWorld())
	{
		SpawnedPatchActor = World->SpawnActor<AActor>(NewBPActorClass, PatchActorSpawnTransform);
		if (!IsValid(SpawnedPatchActor))
		{
			UE_LOG(LogHotUpdate, Warning, TEXT("%s: failed to spawn patch actor class %s."), *GetName(), *NewBPActorClass->GetName());
		}
	}
}

void ABaseHotTestActor::LoadStreamLevel_Implementation(FName LevelPackageName)
{
	if (LevelPackageName.IsNone())
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("%s: cannot load an empty streaming level name."), *GetName());
		return;
	}

	FLatentActionInfo LatentInfo;
	LatentInfo.CallbackTarget = this;
	LatentInfo.UUID = GetUniqueID();
	UGameplayStatics::LoadStreamLevel(this, LevelPackageName, true, false, LatentInfo);
}
