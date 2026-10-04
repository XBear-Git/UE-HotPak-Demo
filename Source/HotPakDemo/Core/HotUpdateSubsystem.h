// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/UniquePtr.h"
#include "HotUpdateSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHotfixApplied, const FString&, NewVersion);

/**
 * Minimal runtime state provider used by the hot-update test scene.
 *
 * Downloading, verification, persistence, and rollback are added by the update
 * pipeline. This subsystem keeps the actor independent from those details while
 * providing the callback contract. Mounting is implemented here as the Day 1
 * manual-mount proof of concept, and will be extracted into UPakMounter later.
 */
UCLASS()
class HOTPAKDEMO_API UHotUpdateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Hot Update")
	FString GetCurrentVersionString() const { return CurrentVersionString; }

	UFUNCTION(BlueprintPure, Category = "Hot Update")
	bool IsPatchMounted() const { return bPatchMounted; }

	/**
	 * Mounts a pak file at runtime (Day 1 manual-mount POC).
	 * The pak's own recorded mount point is used automatically, so it always
	 * aligns with the paths UnrealPak embedded at creation time.
	 *
	 * @param PakFilePath Absolute path to the .pak file.
	 * @return true if the pak was validated and mounted successfully.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update")
	bool MountPak(const FString& PakFilePath);

	/** Returns the filenames of all currently mounted pak files (for verification). */
	UFUNCTION(BlueprintPure, Category = "Hot Update")
	TArray<FString> GetMountedPakFilenames() const;

	/** Called by the mount pipeline after a patch has been mounted. */
	UFUNCTION(BlueprintCallable, Category = "Hot Update")
	void NotifyHotfixApplied(const FString& NewVersion);

	/** Blueprint hook for the test scene and other observers. */
	UPROPERTY(BlueprintAssignable, Category = "Hot Update")
	FOnHotfixApplied OnHotfixApplied;

private:
	/** Console command handler: HotUpdate.MountPak <PakFilePath> */
	void HandleMountPakCommand(const TArray<FString>& Args);

	TUniquePtr<FAutoConsoleCommand> MountPakConsoleCommand;

	UPROPERTY()
	FString CurrentVersionString = TEXT("1.0.0");

	UPROPERTY()
	bool bPatchMounted = false;
};
