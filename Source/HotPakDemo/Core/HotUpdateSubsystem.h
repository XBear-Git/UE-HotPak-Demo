// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "HotUpdateTypes.h"
#include "PakDownloadInfo.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/UniquePtr.h"
#include "UpdateStateMachine.h"
#include "HotUpdateSubsystem.generated.h"

class SUpdatePanel;
class UPakDownloader;
class UPakMounter;
class UPakVerifier;
class UVersionManager;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHotfixApplied, const FString&, NewVersion);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHotUpdateDownloadProgress, int64, BytesReceived, int64, TotalBytes);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHotUpdateRolledBack, const FString&, RevertedVersion);

/**
 * 热更新生命周期管理器与编排者。
 *
 * Day 1~5：手动挂载 → 版本检测 → 下载校验 → 运行时挂载 → 资源生效。
 * Day 6：加健壮性——Mount 失败回滚到基础版、异常全覆盖、Slate 更新面板、耗时统计。
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
	 * 用户确认更新。
	 * 仅在当前状态为 NeedUpdate 时才会开始下载；Slate 面板「更新」按钮与控制台命令均调用它。
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update")
	void ConfirmUpdate();

	/** 手动重试：清除失败记录并重新检测（Slate 面板「重试」按钮）。 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update")
	void RetryUpdate();

	/** 已下载并校验通过的本地 Pak 路径（未就绪时为空）。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update")
	FString GetDownloadedPakPath() const { return DownloadedPakPath; }

	// ---- Day 6：给 UI / 日志用的状态查询 ----

	/** 最近一次检测到的远端版本。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update")
	FString GetRemoteVersionString() const;

	/** 最近一次失败的错误类型。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update")
	EHotUpdateError GetLastError() const { return LastError; }

	/** 最近一次失败的可读文案。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update")
	FString GetLastErrorText() const;

	/** 当前状态的可读文案（供 UI 展示）。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update")
	FString GetStatusText() const;

	/** 下载进度 0..1（未知时为 0）。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update")
	float GetDownloadProgress() const;

	UFUNCTION(BlueprintPure, Category = "Hot Update")
	int64 GetDownloadBytesReceived() const { return DownloadBytesReceived; }

	UFUNCTION(BlueprintPure, Category = "Hot Update")
	int64 GetDownloadBytesTotal() const { return DownloadBytesTotal; }

	/** 最近一次更新的耗时/体积统计。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update")
	FHotUpdateStats GetLastStats() const { return LastStats; }

	/**
	 * 应用/恢复更新面板所需的输入模式（Day 6）。
	 * bUIActive=true：显示鼠标 + GameAndUI，便于点击面板按钮；
	 * bUIActive=false：隐藏鼠标 + GameOnly。
	 * 找不到 PlayerController 时不改变状态，留待下次调用（面板 Tick 会重试）。
	 */
	void ApplyUpdateInputMode(bool bUIActive);

	/** 下载进度广播（子系统转发，供观察者/UI）。 */
	UPROPERTY(BlueprintAssignable, Category = "Hot Update")
	FOnHotUpdateDownloadProgress OnDownloadProgress;

	/** 回滚完成广播（仅通知 UI/日志，不代表资源已挂载）。 */
	UPROPERTY(BlueprintAssignable, Category = "Hot Update")
	FOnHotUpdateRolledBack OnUpdateRolledBack;

	/**
	 * Mounts a pak file at runtime (Day 1 manual-mount POC).
	 *
	 * @param PakFilePath Absolute path to the .pak file.
	 * @return true if the pak was validated and mounted successfully.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update")
	bool MountPak(const FString& PakFilePath);

	/** Returns the filenames of all currently mounted pak files (for verification). */
	UFUNCTION(BlueprintPure, Category = "Hot Update")
	TArray<FString> GetMountedPakFilenames() const;

	/** Called by the mount pipeline after a patch has been mounted (sets bPatchMounted + broadcasts). */
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

	/** Console command handler: HotUpdate.RetryUpdate —— 清除失败记录并重新检测。 */
	void HandleRetryUpdateCommand(const TArray<FString>& Args);

	/** 状态机转移回调：打印可见反馈 + 刷新 Slate 面板 + 转发给外部监听者。 */
	UFUNCTION()
	void HandleUpdateStateChanged(EHotUpdateState OldState, EHotUpdateState NewState);

	/** 版本检测完成回调：ForceUpdate 自动下载（带防死循环守卫），NeedUpdate 等待确认。 */
	UFUNCTION()
	void HandleVersionCheckCompleted(EHotUpdateState ResultState, const FString& RemoteVersion);

	/** 下载完成回调：进入校验，通过则改名，失败则重试/进入终态失败。 */
	UFUNCTION()
	void HandlePakDownloadComplete(bool bSuccess, const FString& LocalPath);

	/** 下载进度回调（转发 downloader 的进度 + 刷新 UI）。 */
	UFUNCTION()
	void HandlePakDownloadProgress(int64 BytesReceived, int64 TotalBytes);

	/** 确保下载器/校验器已创建并绑定委托。 */
	void EnsureDownloadPipeline();

	/** 确保挂载器已创建。 */
	void EnsureMountPipeline();

	/** 启动时只挂载「记录的稳定补丁」；缺失/损坏则回落基础版并重置版本（Day 6）。 */
	void RemountStablePatch();

	/** 校验通过后挂载已下载的 Pak；成功写稳定版本，失败进终态失败（Day 5/6）。 */
	void MountDownloadedPak();

	/** 发起第 DownloadAttempt 次下载尝试（内部使用，不重置计数）。 */
	void BeginDownloadAttempt();

	/** 删除残留的临时文件。 */
	void CleanupTempPakFile();

	/** 一次「下载 + 校验」失败后的统一处理：重试或进入终态失败。 */
	void HandleDownloadAttemptFailed(EHotUpdateError Error, const FString& Reason);

	/** 校验通过：把 `.tmp` 改名为正式 Pak，记录路径并进入挂载。 */
	void FinalizeDownloadedPak();

	/** 构造唯一落地文件名 Patch_<版本>_<sha前8位>.pak（决策 4）。 */
	FString BuildUniquePakFileName() const;

	/** 下载前的磁盘空间预检（Day 6）。 */
	bool PreflightDiskSpace(int64 RequiredBytes) const;

	/** 终态失败统一入口：记错误 → Failed → 回滚（仅 Mount 失败做实质回滚）。 */
	void HandleTerminalFailure(EHotUpdateError Error);

	/** 回滚：清理坏包/临时文件 + 重置稳定版本（仅 Mount 失败做实质回滚）。 */
	void Rollback(bool bUndoStable);

	/** 删除本次下载产生的正式/临时文件（不触碰稳定补丁）。 */
	void CleanupDownloadArtifacts();

	/** 打印最近一次统计日志。 */
	void LogStats() const;

	/** 刷新 Slate 面板（若已创建）。 */
	void RefreshUpdatePanel();

	/** 引擎循环初始化完成：此时 GameViewport 已就绪，创建并挂上 Slate 面板。 */
	void HandleEngineLoopInitComplete();

	/** 创建 Slate 面板并加入视口。 */
	void CreateUpdatePanel();

	/** 从视口移除 Slate 面板。 */
	void DestroyUpdatePanel();

	TUniquePtr<FAutoConsoleCommand> MountPakConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> CheckForUpdateConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> StatusConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> ConfirmUpdateConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> SelfTestConsoleCommand;
	TUniquePtr<FAutoConsoleCommand> RetryUpdateConsoleCommand;

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

	/** 已校验通过的正式 Pak 路径（供挂载）。 */
	UPROPERTY(Transient)
	FString DownloadedPakPath;

	/** 当前是第几次「下载 + 校验」尝试（1 起）。 */
	int32 DownloadAttempt = 0;

	/** 最大尝试次数（总次数）。 */
	static constexpr int32 MaxDownloadAttempts = 3;

	/** 补丁 Pak 默认挂载优先级（高于基础包 order=0）。 */
	static constexpr int32 DefaultPatchPakOrder = 100;

	/** 下载前磁盘空间预检的余量（字节）。 */
	static constexpr int64 RequiredDiskSpaceMargin = 32ll * 1024ll * 1024ll;

	/** Day 6：错误原因。 */
	EHotUpdateError LastError = EHotUpdateError::None;

	/** Day 6：最近一次统计。 */
	FHotUpdateStats LastStats;

	/** Day 6：下载进度缓存。 */
	int64 DownloadBytesReceived = 0;
	int64 DownloadBytesTotal = 0;

	/** Day 6：各阶段计时起点（FPlatformTime::Seconds）。 */
	double AttemptStartTime = 0.0;
	double DownloadStartTime = 0.0;
	double VerifyStartTime = 0.0;
	double MountStartTime = 0.0;

	/** Day 6：最近一次尝试是否成功（供 UI 区分 Done 是成功还是回滚后）。 */
	bool bLastAttemptSucceeded = false;

	/** Day 6：当前是否已应用「UI 输入模式」（用于避免每帧重复设置）。 */
	bool bUpdateInputModeApplied = false;

	/** Day 6：Slate 面板与引擎循环回调句柄。 */
	TSharedPtr<SUpdatePanel> UpdatePanel;
	FDelegateHandle EngineLoopInitHandle;

	UPROPERTY()
	FString CurrentVersionString = TEXT("1.0.0");

	UPROPERTY()
	bool bPatchMounted = false;
};
