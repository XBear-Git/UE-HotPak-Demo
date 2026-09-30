// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HotUpdateSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHotfixApplied, const FString&, NewVersion);

/**
 * Minimal runtime state provider used by the hot-update test scene.
 *
 * Downloading, verification, mounting, persistence, and rollback are added
 * by the update pipeline. This subsystem keeps the actor independent from
 * those implementation details while providing the callback contract.
 */
UCLASS()
class HOTPAKDEMO_API UHotUpdateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Hot Update")
	FString GetCurrentVersionString() const { return CurrentVersionString; }

	UFUNCTION(BlueprintPure, Category = "Hot Update")
	bool IsPatchMounted() const { return bPatchMounted; }

	/** Called by the future mount pipeline after a patch has been mounted. */
	UFUNCTION(BlueprintCallable, Category = "Hot Update")
	void NotifyHotfixApplied(const FString& NewVersion);

	/** Blueprint hook for the test scene and other observers. */
	UPROPERTY(BlueprintAssignable, Category = "Hot Update")
	FOnHotfixApplied OnHotfixApplied;

private:
	UPROPERTY()
	FString CurrentVersionString = TEXT("1.0.0");

	UPROPERTY()
	bool bPatchMounted = false;
};
