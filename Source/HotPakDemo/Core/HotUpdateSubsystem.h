// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/UniquePtr.h"
#include "UpdateStateMachine.h"
#include "HotUpdateSubsystem.generated.h"

class UVersionManager;

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
	 * 启动版本检测（Day 3）。
	 * 拉取远端 version.json，与本地持久化版本比对，输出
	 * 「需更新 / 已最新 / 强制更新」并驱动状态机。
	 * Initialize() 时自动调用一次，也可手动重测（控制台 HotUpdate.CheckForUpdate）。
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update")
	void StartUpdateCheck();

	/** 当前更新状态（由内部状态机提供）。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update")
	EHotUpdateState GetUpdateState() const;

	/** 更新状态变化广播（转发自 UUpdateStateMachine），供蓝图/场景监听。 */
	UPROPERTY(BlueprintAssignable, Category = "Hot Update")
	FOnHotUpdateStateChanged OnUpdateStateChanged;

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

	/** Console command handler: HotUpdate.CheckForUpdate */
	void HandleCheckForUpdateCommand(const TArray<FString>& Args);

	/** Console command handler: HotUpdate.Status —— 打印本地/远端版本与当前状态。 */
	void HandleStatusCommand(const TArray<FString>& Args);

	/** 状态机转移回调：打印可见反馈并转发给外部监听者。 */
	UFUNCTION()
	void HandleUpdateStateChanged(EHotUpdateState OldState, EHotUpdateState NewState);

	TUniquePtr<FAutoConsoleCommand> MountPakConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> CheckForUpdateConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> StatusConsoleCommand;

	/** 更新流程状态机（Day 3 新增）。 */
	UPROPERTY(Transient)
	TObjectPtr<UUpdateStateMachine> UpdateStateMachine;

	/** 版本检测与比对管理器（Day 3 新增）。 */
	UPROPERTY(Transient)
	TObjectPtr<UVersionManager> VersionManager;

	UPROPERTY()
	FString CurrentVersionString = TEXT("1.0.0");

	UPROPERTY()
	bool bPatchMounted = false;
};
