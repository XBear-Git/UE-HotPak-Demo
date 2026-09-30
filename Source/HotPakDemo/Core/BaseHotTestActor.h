// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BaseHotTestActor.generated.h"

class UDataTable;
class UHotUpdateSubsystem;
class UTexture2D;

/**
 * Base actor for the multi-type hot-update verification scene.
 *
 * The actor owns no update pipeline logic. It observes UHotUpdateSubsystem,
 * exposes resource slots to Blueprint, and provides a single manual refresh
 * entry point for validating texture, data table, actor, and level updates.
 */
UCLASS(Abstract, Blueprintable)
class HOTPAKDEMO_API ABaseHotTestActor : public AActor
{
	GENERATED_BODY()

public:
	ABaseHotTestActor();

	/** Returns the version supplied by UHotUpdateSubsystem, or the fallback value. */
	UFUNCTION(BlueprintPure, Category = "Hot Test|State")
	FString GetCurrentVersionString() const;

	/** Returns whether the active update subsystem has mounted a patch. */
	UFUNCTION(BlueprintPure, Category = "Hot Test|State")
	bool IsPatchMounted() const;

	/** Reloads every configured resource slot. Intended for debug buttons and QA. */
	UFUNCTION(BlueprintCallable, Category = "Hot Test|State")
	void ForceReloadAllSlots();

	/** Assigns the texture slot to a Blueprint-owned material or component. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Hot Test|Slots")
	void UpdateTexture(UTexture2D* NewTex);

	/** Reads the updated table and applies its values to Blueprint-owned UI/gameplay. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Hot Test|Slots")
	void UpdateDataTable(UDataTable* NewDT);

	/** Replaces the currently spawned patch actor with the supplied Blueprint class. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Hot Test|Slots")
	void SpawnPatchActor(TSubclassOf<AActor> NewBPActorClass);

	/** Loads the configured streaming level package. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Hot Test|Slots")
	void LoadStreamLevel(FName LevelPackageName);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Called after the subsystem reports a successful patch mount. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Hot Test|Events")
	void OnHotfixApplied();

	/** Registers this actor with the active game-instance update subsystem. */
	void RegisterHotfixCompleteCallback();

	UFUNCTION()
	void HandleHotfixApplied(const FString& NewVersion);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hot Test|Resources")
	TSoftObjectPtr<UTexture2D> HotfixTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hot Test|Resources")
	TSoftObjectPtr<UDataTable> HotfixDataTable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hot Test|Resources")
	TSoftClassPtr<AActor> HotfixActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hot Test|Resources")
	FName HotfixStreamLevel = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hot Test|Resources")
	FTransform PatchActorSpawnTransform;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Hot Test|State")
	TObjectPtr<AActor> SpawnedPatchActor;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hot Test|State")
	FString FallbackVersionString = TEXT("1.0.0");

	TWeakObjectPtr<UHotUpdateSubsystem> HotUpdateSubsystem;
};
