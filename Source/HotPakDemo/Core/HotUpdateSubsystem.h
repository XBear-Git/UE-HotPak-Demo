// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "PakDownloadInfo.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/UniquePtr.h"
#include "UpdateStateMachine.h"
#include "HotUpdateSubsystem.generated.h"

class UPakDownloader;
class UPakMounter;
class UPakVerifier;
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
	 * 开始下载增量 Pak（Day 4）。
	 * 从 VersionManager 取 pak 下载信息，先落 `.tmp`，校验通过后改名 `.pak`；失败自动重试。
	 * ForceUpdate 会由状态回调自动触发；NeedUpdate 需先调用 ConfirmUpdate()。
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update")
	void StartDownload();

	/**
	 * 用户确认更新（Day 4 用控制台 `HotUpdate.ConfirmUpdate` 模拟，Day 6 换成 UI 按钮）。
	 * 仅在当前状态为 NeedUpdate 时才会开始下载。
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update")
	void ConfirmUpdate();

	/** 已下载并校验通过的本地 Pak 路径（供 Day 5 挂载使用；未就绪时为空）。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update")
	FString GetDownloadedPakPath() const { return DownloadedPakPath; }

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

	/** Console command handler: HotUpdate.ConfirmUpdate —— 模拟用户确认更新。 */
	void HandleConfirmUpdateCommand(const TArray<FString>& Args);

	/** Console command handler: HotUpdate.SelfTest —— 运行 SHA-256 已知向量自测。 */
	void HandleSelfTestCommand(const TArray<FString>& Args);

	/** 状态机转移回调：打印可见反馈并转发给外部监听者。 */
	UFUNCTION()
	void HandleUpdateStateChanged(EHotUpdateState OldState, EHotUpdateState NewState);

	/** 版本检测完成回调：ForceUpdate 自动下载，NeedUpdate 等待确认。 */
	UFUNCTION()
	void HandleVersionCheckCompleted(EHotUpdateState ResultState, const FString& RemoteVersion);

	/** 下载完成回调：进入校验，通过则改名，失败则重试/进入 Failed。 */
	UFUNCTION()
	void HandlePakDownloadComplete(bool bSuccess, const FString& LocalPath);

	/** 确保下载器/校验器已创建并绑定委托。 */
	void EnsureDownloadPipeline();

	/** 确保挂载器已创建（Day 5）。 */
	void EnsureMountPipeline();

	/** 启动时重挂载 Saved/HotUpdate 下的本地补丁，保证重启后资源与版本一致（Day 5）。 */
	void RemountLocalPatches();

	/** 校验通过后挂载已下载的 Pak，并触发生效链 + 持久化本地版本（Day 5）。 */
	void MountDownloadedPak();

	/** 发起第 DownloadAttempt 次下载尝试（内部使用，不重置计数）。 */
	void BeginDownloadAttempt();

	/** 删除残留的 `.tmp` 临时文件。 */
	void CleanupTempPakFile();

	/** 一次「下载 + 校验」失败后的统一处理：重试或进入 Failed。 */
	void HandleDownloadAttemptFailed(const FString& Reason);

	/** 校验通过：把 `.tmp` 改名为 `.pak`，记录路径并结束本次流程。 */
	void FinalizeDownloadedPak();

	TUniquePtr<FAutoConsoleCommand> MountPakConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> CheckForUpdateConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> StatusConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> ConfirmUpdateConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> SelfTestConsoleCommand;

	/** 更新流程状态机（Day 3 新增）。 */
	UPROPERTY(Transient)
	TObjectPtr<UUpdateStateMachine> UpdateStateMachine;

	/** 版本检测与比对管理器（Day 3 新增）。 */
	UPROPERTY(Transient)
	TObjectPtr<UVersionManager> VersionManager;

	/** 下载器（Day 4 新增）。 */
	UPROPERTY(Transient)
	TObjectPtr<UPakDownloader> PakDownloader;

	/** 校验器（Day 4 新增）。 */
	UPROPERTY(Transient)
	TObjectPtr<UPakVerifier> PakVerifier;

	/** Pak 挂载器（Day 5 新增）。 */
	UPROPERTY(Transient)
	TObjectPtr<UPakMounter> PakMounter;

	/** 本次要下载的 Pak 信息。 */
	UPROPERTY(Transient)
	FPakDownloadInfo PendingPakInfo;

	/** 临时下载路径（`.pak.tmp`）。 */
	UPROPERTY(Transient)
	FString TempPakPath;

	/** 已校验通过的正式 Pak 路径（供 Day 5 挂载）。 */
	UPROPERTY(Transient)
	FString DownloadedPakPath;

	/** 当前是第几次「下载 + 校验」尝试（1 起）。 */
	int32 DownloadAttempt = 0;

	/** 最大尝试次数（总次数）。 */
	static constexpr int32 MaxDownloadAttempts = 3;

	/** 补丁 Pak 默认挂载优先级（高于基础包 order=0）。 */
	static constexpr int32 DefaultPatchPakOrder = 100;

	UPROPERTY()
	FString CurrentVersionString = TEXT("1.0.0");

	UPROPERTY()
	bool bPatchMounted = false;
};
