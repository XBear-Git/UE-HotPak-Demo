// Copyright Epic Games, Inc. All Rights Reserved.

#include "HotUpdateSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "IPlatformFilePak.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "PakDownloader.h"
#include "PakMounter.h"
#include "PakVerifier.h"
#include "SHA256.h"
#include "UpdatePanel.h"
#include "VersionManager.h"
#include "VersionRecord.h"

#include "../HotPakDemo.h"

void UHotUpdateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Console command for manual testing in a packaged build:
	//   HotUpdate.MountPak "D:/path/to/test.pak"
	MountPakConsoleCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("HotUpdate.MountPak"),
		TEXT("Mount a pak file at runtime. Usage: HotUpdate.MountPak <PakFilePath>"),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &UHotUpdateSubsystem::HandleMountPakCommand)
	);

	// Console command to re-run the Day 3 version check without restarting:
	//   HotUpdate.CheckForUpdate
	CheckForUpdateConsoleCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("HotUpdate.CheckForUpdate"),
		TEXT("Re-run the update check (GET version.json and compare versions)."),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &UHotUpdateSubsystem::HandleCheckForUpdateCommand)
	);

	// Console command to inspect the current update status:
	//   HotUpdate.Status
	StatusConsoleCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("HotUpdate.Status"),
		TEXT("Print local version, remote version and current update state."),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &UHotUpdateSubsystem::HandleStatusCommand)
	);

	// Console command that simulates user confirmation of a pending update:
	//   HotUpdate.ConfirmUpdate
	ConfirmUpdateConsoleCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("HotUpdate.ConfirmUpdate"),
		TEXT("Confirm a pending (NeedUpdate) patch download."),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &UHotUpdateSubsystem::HandleConfirmUpdateCommand)
	);

	// Console command to run the built-in SHA-256 known-vector self test:
	//   HotUpdate.SelfTest
	SelfTestConsoleCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("HotUpdate.SelfTest"),
		TEXT("Run the built-in SHA-256 known-vector self test."),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &UHotUpdateSubsystem::HandleSelfTestCommand)
	);

	// Console command to retry after a failure (Day 6):
	//   HotUpdate.RetryUpdate
	RetryUpdateConsoleCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("HotUpdate.RetryUpdate"),
		TEXT("Clear the failed-version guard and re-run the update check."),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &UHotUpdateSubsystem::HandleRetryUpdateCommand)
	);

	// Day 3: create the update state machine and the version manager, wire the
	// state broadcast through to this subsystem, then run the first check.
	UpdateStateMachine = NewObject<UUpdateStateMachine>(this);
	VersionManager = NewObject<UVersionManager>(this);

	if (UpdateStateMachine && VersionManager)
	{
		UpdateStateMachine->OnStateChanged.AddDynamic(this, &UHotUpdateSubsystem::HandleUpdateStateChanged);
		VersionManager->OnVersionCheckCompleted.AddDynamic(this, &UHotUpdateSubsystem::HandleVersionCheckCompleted);
		VersionManager->Initialize(UpdateStateMachine);
	}

	// Day 5: Pak 挂载器。
	PakMounter = NewObject<UPakMounter>(this);

	// Run the SHA-256 self test once at startup so a broken hash implementation
	// is caught immediately (a mismatch here would break every patch verification).
#if !UE_BUILD_SHIPPING
	HotUpdateSha256::RunSelfTest();
#endif

	// Day 6: Slate 更新面板——引擎循环初始化完成（GameViewport 就绪）后再挂到视口。
	EngineLoopInitHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddUObject(
		this, &UHotUpdateSubsystem::HandleEngineLoopInitComplete);
	CreateUpdatePanel(); // 若此刻已就绪则立即创建；否则等回调

	// Day 5/6: 启动时只挂载「记录的稳定补丁」，再做版本检测。
	RemountStablePatch();

	StartUpdateCheck();
}

void UHotUpdateSubsystem::Deinitialize()
{
	if (EngineLoopInitHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineLoopInitHandle);
		EngineLoopInitHandle.Reset();
	}
	DestroyUpdatePanel();

	MountPakConsoleCommand.Reset();
	CheckForUpdateConsoleCommand.Reset();
	StatusConsoleCommand.Reset();
	ConfirmUpdateConsoleCommand.Reset();
	SelfTestConsoleCommand.Reset();
	RetryUpdateConsoleCommand.Reset();
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------
// 挂载（Day 1 / Day 5）
// ---------------------------------------------------------------------------

bool UHotUpdateSubsystem::MountPak(const FString& PakFilePath)
{
	EnsureMountPipeline();

	if (!PakMounter || !PakMounter->MountPak(PakFilePath, DefaultPatchPakOrder))
	{
		return false;
	}

	const FString AppliedVersion = (VersionManager && !VersionManager->GetRemoteVersion().IsEmpty())
		? VersionManager->GetRemoteVersion()
		: FString(TEXT("1.1.0"));
	NotifyHotfixApplied(AppliedVersion);
	return true;
}

TArray<FString> UHotUpdateSubsystem::GetMountedPakFilenames() const
{
	return PakMounter ? PakMounter->GetMountedPakFilenames() : TArray<FString>();
}

void UHotUpdateSubsystem::NotifyHotfixApplied(const FString& NewVersion)
{
	if (!NewVersion.IsEmpty())
	{
		CurrentVersionString = NewVersion;
	}

	bPatchMounted = true;
	OnHotfixApplied.Broadcast(CurrentVersionString);
}

// ---------------------------------------------------------------------------
// 控制台命令
// ---------------------------------------------------------------------------

void UHotUpdateSubsystem::HandleMountPakCommand(const TArray<FString>& Args)
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[HotUpdate] Usage: HotUpdate.MountPak <PakFilePath>"));
		return;
	}

	FString PakPath = FString::Join(Args, TEXT(" "));
	PakPath.TrimStartAndEndInline();
	if (PakPath.Len() >= 2 && PakPath[0] == TEXT('"') && PakPath[PakPath.Len() - 1] == TEXT('"'))
	{
		PakPath = PakPath.Mid(1, PakPath.Len() - 2);
	}

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] MountPak command received: '%s'"), *PakPath);
	MountPak(PakPath);
}

void UHotUpdateSubsystem::HandleCheckForUpdateCommand(const TArray<FString>& Args)
{
	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 手动触发版本检测。"));
	StartUpdateCheck();
}

void UHotUpdateSubsystem::HandleStatusCommand(const TArray<FString>& Args)
{
	const FString LocalVersion = VersionManager
		? VersionManager->QueryLocalVersion()
		: FString(TEXT("<未初始化>"));
	const FString RemoteVersion = (VersionManager && !VersionManager->GetRemoteVersion().IsEmpty())
		? VersionManager->GetRemoteVersion()
		: FString(TEXT("<未知，尚未成功检测>"));
	const FString StateName = UUpdateStateMachine::GetStateDisplayName(GetUpdateState());
	const FString StablePak = VersionManager ? VersionManager->GetStablePakFileName() : FString();

	const FString Message = FString::Printf(
		TEXT("HotUpdate 状态：本地版本=%s，远端版本=%s，当前状态=%s，稳定补丁=%s，错误=%s"),
		*LocalVersion, *RemoteVersion, *StateName,
		StablePak.IsEmpty() ? TEXT("<无>") : *StablePak,
		*GetLastErrorText());

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] %s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Cyan, Message);
	}
}

void UHotUpdateSubsystem::HandleConfirmUpdateCommand(const TArray<FString>& Args)
{
	ConfirmUpdate();
}

void UHotUpdateSubsystem::HandleRetryUpdateCommand(const TArray<FString>& Args)
{
	RetryUpdate();
}

void UHotUpdateSubsystem::HandleSelfTestCommand(const TArray<FString>& Args)
{
	const bool bPassed = HotUpdateSha256::RunSelfTest();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 10.0f, bPassed ? FColor::Green : FColor::Red,
			bPassed ? TEXT("SHA-256 自测通过") : TEXT("SHA-256 自测失败（见日志）"));
	}
}

// ---------------------------------------------------------------------------
// 版本检测
// ---------------------------------------------------------------------------

void UHotUpdateSubsystem::StartUpdateCheck()
{
	if (!VersionManager)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[HotUpdate] StartUpdateCheck: VersionManager 尚未初始化。"));
		return;
	}

	VersionManager->CheckForUpdate();
}

EHotUpdateState UHotUpdateSubsystem::GetUpdateState() const
{
	return UpdateStateMachine ? UpdateStateMachine->GetCurrentState() : EHotUpdateState::Idle;
}

void UHotUpdateSubsystem::HandleVersionCheckCompleted(EHotUpdateState ResultState, const FString& RemoteVersion)
{
	if (ResultState == EHotUpdateState::ForceUpdate)
	{
		const FString Remote = VersionManager ? VersionManager->GetRemoteVersion() : FString();
		const FString LastFailed = VersionManager ? VersionManager->GetLastFailedVersion() : FString();

		// 防 ForceUpdate 跨重启死循环：同一版本上次更新失败则不再自动下载。
		if (!LastFailed.IsEmpty() && LastFailed == Remote)
		{
			UE_LOG(LogHotUpdate, Warning,
				TEXT("[HotUpdate] 强制更新被跳过：版本 %s 上次更新失败，请手动重试（HotUpdate.RetryUpdate 或界面按钮）。"), *Remote);
			RefreshUpdatePanel();
			return;
		}

		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 强制更新，自动开始下载。"));
		StartDownload();
	}
	else if (ResultState == EHotUpdateState::NeedUpdate)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 需更新，等待用户确认（HotUpdate.ConfirmUpdate 或界面按钮）。"));
	}
	else if (ResultState == EHotUpdateState::Failed)
	{
		// 清单拉取 / 解析失败：统一走终态失败（no-op 回滚，稳定版不受影响）。
		HandleTerminalFailure(EHotUpdateError::ManifestParse);
	}
}

// ---------------------------------------------------------------------------
// 下载 + 校验（Day 4 / Day 6）
// ---------------------------------------------------------------------------

void UHotUpdateSubsystem::StartDownload()
{
	if (!VersionManager)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] StartDownload: VersionManager 未初始化。"));
		return;
	}

	const FPakDownloadInfo Info = VersionManager->GetPakDownloadInfo();
	if (!Info.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] 下载信息缺失（version.json 无 pak.url），无法下载。"));
		HandleTerminalFailure(EHotUpdateError::ManifestParse);
		return;
	}

	// 重置本次流程状态
	LastError = EHotUpdateError::None;
	bLastAttemptSucceeded = false;
	DownloadBytesReceived = 0;
	DownloadBytesTotal = 0;
	LastStats = FHotUpdateStats();
	AttemptStartTime = FPlatformTime::Seconds();

	PendingPakInfo = Info;
	DownloadedPakPath.Reset();

	// 下载前磁盘空间预检（不足则直接终态失败，不重试）。
	if (!PreflightDiskSpace(PendingPakInfo.Size))
	{
		HandleTerminalFailure(EHotUpdateError::DiskSpace);
		return;
	}

	const FString PakDirectory = UPakMounter::GetLocalPatchDirectory();
	IFileManager::Get().MakeDirectory(*PakDirectory, true);

	const FString UniqueFileName = BuildUniquePakFileName();
	TempPakPath = FPaths::Combine(PakDirectory, UniqueFileName + TEXT(".tmp"));
	DownloadAttempt = 0;

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 下载目录：%s，落地名：%s"), *PakDirectory, *UniqueFileName);

	EnsureDownloadPipeline();
	BeginDownloadAttempt();
}

void UHotUpdateSubsystem::ConfirmUpdate()
{
	if (GetUpdateState() != EHotUpdateState::NeedUpdate)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[HotUpdate] ConfirmUpdate: 当前状态为 %s，不是 NeedUpdate，已忽略。"),
			*UUpdateStateMachine::GetStateDisplayName(GetUpdateState()));
		return;
	}

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 用户已确认更新，开始下载。"));
	StartDownload();
}

void UHotUpdateSubsystem::RetryUpdate()
{
	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 用户重试：清除失败记录并重新检测。"));
	if (VersionManager)
	{
		VersionManager->SaveLastFailedVersion(FString());
	}
	LastError = EHotUpdateError::None;
	bLastAttemptSucceeded = false;
	StartUpdateCheck();
}

FString UHotUpdateSubsystem::BuildUniquePakFileName() const
{
	const FString Remote = (VersionManager && !VersionManager->GetRemoteVersion().IsEmpty())
		? VersionManager->GetRemoteVersion()
		: FString(TEXT("unknown"));
	const FString Sha8 = PendingPakInfo.Sha256.Left(8);
	return FString::Printf(TEXT("Patch_%s_%s.pak"), *Remote, *Sha8);
}

bool UHotUpdateSubsystem::PreflightDiskSpace(int64 RequiredBytes) const
{
	if (RequiredBytes <= 0)
	{
		return true;
	}

	const FString Dir = UPakMounter::GetLocalPatchDirectory();
	uint64 Total = 0;
	uint64 Free = 0;
	if (!FPlatformMisc::GetDiskTotalAndFreeSpace(Dir, Total, Free))
	{
		// 查询失败不阻断下载。
		return true;
	}

	const int64 Available = int64(Free);
	if (Available < RequiredBytes + RequiredDiskSpaceMargin)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate][Disk] 磁盘空间不足：需要 %lld + 余量 %lld，可用 %lld。"),
			RequiredBytes, RequiredDiskSpaceMargin, Available);
		return false;
	}

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate][Disk] 磁盘空间检查通过：需要 %lld，可用 %lld。"), RequiredBytes, Available);
	return true;
}

void UHotUpdateSubsystem::EnsureDownloadPipeline()
{
	if (!PakDownloader)
	{
		PakDownloader = NewObject<UPakDownloader>(this);
		PakDownloader->OnComplete.AddDynamic(this, &UHotUpdateSubsystem::HandlePakDownloadComplete);
		PakDownloader->OnProgress.AddDynamic(this, &UHotUpdateSubsystem::HandlePakDownloadProgress);
	}
	if (!PakVerifier)
	{
		PakVerifier = NewObject<UPakVerifier>(this);
	}
}

void UHotUpdateSubsystem::BeginDownloadAttempt()
{
	++DownloadAttempt;
	DownloadStartTime = FPlatformTime::Seconds();
	DownloadBytesReceived = 0;
	DownloadBytesTotal = 0;

	if (UpdateStateMachine)
	{
		UpdateStateMachine->TransitionTo(EHotUpdateState::Downloading);
	}

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 第 %d/%d 次尝试：下载 %s"),
		DownloadAttempt, MaxDownloadAttempts, *PendingPakInfo.Url);

	PakDownloader->StartDownload(PendingPakInfo.Url, TempPakPath);
}

void UHotUpdateSubsystem::HandlePakDownloadProgress(int64 BytesReceived, int64 TotalBytes)
{
	DownloadBytesReceived = BytesReceived;
	DownloadBytesTotal = TotalBytes;
	OnDownloadProgress.Broadcast(BytesReceived, TotalBytes);
	RefreshUpdatePanel();
}

void UHotUpdateSubsystem::HandlePakDownloadComplete(bool bSuccess, const FString& LocalPath)
{
	if (!bSuccess)
	{
		HandleDownloadAttemptFailed(EHotUpdateError::Network, TEXT("下载失败（连接失败 / HTTP 非 200 / 写入失败）"));
		return;
	}

	LastStats.DownloadSeconds = FPlatformTime::Seconds() - DownloadStartTime;
	LastStats.PakSize = PendingPakInfo.Size;

	if (UpdateStateMachine)
	{
		UpdateStateMachine->TransitionTo(EHotUpdateState::Verifying);
	}

	VerifyStartTime = FPlatformTime::Seconds();
	const EPakVerifyResult Result = PakVerifier
		? PakVerifier->Verify(LocalPath, PendingPakInfo.Sha256, PendingPakInfo.Size)
		: EPakVerifyResult::ReadError;
	LastStats.VerifySeconds = FPlatformTime::Seconds() - VerifyStartTime;

	if (Result == EPakVerifyResult::Success)
	{
		FinalizeDownloadedPak();
		return;
	}

	EHotUpdateError Error = EHotUpdateError::HashMismatch;
	switch (Result)
	{
	case EPakVerifyResult::SizeMismatch: Error = EHotUpdateError::SizeMismatch; break;
	case EPakVerifyResult::HashMismatch: Error = EHotUpdateError::HashMismatch; break;
	case EPakVerifyResult::FileNotFound:
	case EPakVerifyResult::ReadError:    Error = EHotUpdateError::FileIO; break;
	default: break;
	}
	HandleDownloadAttemptFailed(Error, UPakVerifier::GetResultDisplayName(Result));
}

void UHotUpdateSubsystem::HandleDownloadAttemptFailed(EHotUpdateError Error, const FString& Reason)
{
	UE_LOG(LogHotUpdate, Warning, TEXT("[HotUpdate] 第 %d/%d 次尝试失败：%s"),
		DownloadAttempt, MaxDownloadAttempts, *Reason);
	CleanupTempPakFile();

	if (DownloadAttempt < MaxDownloadAttempts)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[HotUpdate] 准备重试（第 %d/%d 次）..."),
			DownloadAttempt + 1, MaxDownloadAttempts);
		BeginDownloadAttempt();
	}
	else
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] 重试次数耗尽（共 %d 次）。"), MaxDownloadAttempts);
		HandleTerminalFailure(Error);
	}
}

void UHotUpdateSubsystem::CleanupTempPakFile()
{
	if (!TempPakPath.IsEmpty() && IFileManager::Get().FileExists(*TempPakPath))
	{
		IFileManager::Get().Delete(*TempPakPath, false, true, true);
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 已删除临时文件：%s"), *TempPakPath);
	}
}

void UHotUpdateSubsystem::FinalizeDownloadedPak()
{
	FString FinalPath = TempPakPath;
	if (FinalPath.EndsWith(TEXT(".tmp")))
	{
		FinalPath.LeftChopInline(4);
	}

	if (!IFileManager::Get().Move(*FinalPath, *TempPakPath, true, true))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] 改名失败：%s -> %s"), *TempPakPath, *FinalPath);
		HandleDownloadAttemptFailed(EHotUpdateError::FileIO, TEXT("改名为 .pak 失败"));
		return;
	}

	DownloadedPakPath = FinalPath;
	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 校验通过，已改名为正式 Pak：%s"), *FinalPath);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Green,
			FString::Printf(TEXT("热更包已下载并校验通过：%s"), *FPaths::GetCleanFilename(FinalPath)));
	}

	if (UpdateStateMachine)
	{
		UpdateStateMachine->TransitionTo(EHotUpdateState::Mounting);
	}
	MountDownloadedPak();
}

// ---------------------------------------------------------------------------
// 挂载 → 生效 → 持久化（Day 5 / Day 6）
// ---------------------------------------------------------------------------

void UHotUpdateSubsystem::EnsureMountPipeline()
{
	if (!PakMounter)
	{
		PakMounter = NewObject<UPakMounter>(this);
	}
}

void UHotUpdateSubsystem::RemountStablePatch()
{
	EnsureMountPipeline();

	const FString PatchDirectory = UPakMounter::GetLocalPatchDirectory();
	const FString StableFile = VersionManager ? VersionManager->GetStablePakFileName() : FString();

	if (StableFile.IsEmpty())
	{
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 启动重挂载：无稳定补丁记录（基础版）。"));
		return;
	}

	const FString StablePath = FPaths::Combine(PatchDirectory, StableFile);
	if (!IFileManager::Get().FileExists(*StablePath))
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[HotUpdate] 记录的稳定补丁缺失：%s，回落基础版并重置版本。"), *StableFile);
		if (VersionManager)
		{
			VersionManager->SaveStableInfo(UVersionRecord::GetBaseVersion(), FString());
		}
		return;
	}

	const FString StableVersion = VersionManager ? VersionManager->QueryLocalVersion() : FString();

	if (PakMounter->MountPak(StablePath, DefaultPatchPakOrder))
	{
		// 挂载成功：补通知链（#11），让测试场景在 BeginPlay 刷新为补丁资源。
		NotifyHotfixApplied(StableVersion);
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 启动重挂载稳定补丁成功：%s（版本 %s）。"), *StableFile, *StableVersion);
	}
	else
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] 启动重挂载稳定补丁失败（文件损坏？）：%s，删除并回落基础版。"), *StableFile);
		IFileManager::Get().Delete(*StablePath, false, true, true);
		bPatchMounted = false;
		CurrentVersionString = UVersionRecord::GetBaseVersion();
		if (VersionManager)
		{
			VersionManager->SaveLastFailedVersion(StableVersion);
			VersionManager->SaveStableInfo(UVersionRecord::GetBaseVersion(), FString());
		}
	}
}

void UHotUpdateSubsystem::MountDownloadedPak()
{
	if (DownloadedPakPath.IsEmpty())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] MountDownloadedPak: 本地 Pak 路径为空，无法挂载。"));
		HandleTerminalFailure(EHotUpdateError::FileIO);
		return;
	}

	EnsureMountPipeline();

	const int32 MountOrder = (PendingPakInfo.Order > 0) ? PendingPakInfo.Order : DefaultPatchPakOrder;
	MountStartTime = FPlatformTime::Seconds();
	const bool bMounted = PakMounter->MountPak(DownloadedPakPath, MountOrder);
	LastStats.MountSeconds = FPlatformTime::Seconds() - MountStartTime;

	if (!bMounted)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] 挂载下载的 Pak 失败：%s"), *DownloadedPakPath);
		HandleTerminalFailure(EHotUpdateError::MountFailed);
		return;
	}

	// 资源生效链：通知测试场景重载资源（Day 1 已有链路）。
	const FString RemoteVersion = VersionManager ? VersionManager->GetRemoteVersion() : FString();
	NotifyHotfixApplied(RemoteVersion);

	// FR-08 唯一写入点：挂载成功后写入稳定版本 + 稳定补丁文件名。
	if (VersionManager)
	{
		VersionManager->SaveStableInfo(
			RemoteVersion.IsEmpty() ? UVersionRecord::GetBaseVersion() : RemoteVersion,
			FPaths::GetCleanFilename(DownloadedPakPath));
	}

	LastStats.Attempts = DownloadAttempt;
	LastStats.TotalSeconds = FPlatformTime::Seconds() - AttemptStartTime;
	LastError = EHotUpdateError::None;
	bLastAttemptSucceeded = true;
	LogStats();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Green,
			FString::Printf(TEXT("更新完成：版本 %s"), *CurrentVersionString));
	}

	if (UpdateStateMachine)
	{
		UpdateStateMachine->TransitionTo(EHotUpdateState::Done);
	}
	RefreshUpdatePanel();
}

// ---------------------------------------------------------------------------
// 终态失败 + 回滚（Day 6）
// ---------------------------------------------------------------------------

void UHotUpdateSubsystem::HandleTerminalFailure(EHotUpdateError Error)
{
	LastError = Error;
	UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] 更新失败：%s"), *GetLastErrorText());

	LastStats.Attempts = DownloadAttempt;
	if (AttemptStartTime > 0.0)
	{
		LastStats.TotalSeconds = FPlatformTime::Seconds() - AttemptStartTime;
	}
	LogStats();

	if (UpdateStateMachine)
	{
		UpdateStateMachine->TransitionTo(EHotUpdateState::Failed);
	}

	// 只有 Mount 失败需要实质回滚（卸载/删包/重置稳定版）；其余为清理残留的 no-op。
	Rollback(Error == EHotUpdateError::MountFailed);
}

void UHotUpdateSubsystem::Rollback(bool bUndoStable)
{
	if (UpdateStateMachine)
	{
		UpdateStateMachine->TransitionTo(EHotUpdateState::RollingBack);
	}

	UE_LOG(LogHotUpdate, Warning, TEXT("[HotUpdate] 开始回滚（%s）。"),
		bUndoStable ? TEXT("实质回滚：卸载坏包 + 重置稳定版") : TEXT("仅清理残留，稳定版不变"));

	if (bUndoStable && PakMounter && !DownloadedPakPath.IsEmpty())
	{
		PakMounter->UnmountPak(DownloadedPakPath);
	}

	CleanupDownloadArtifacts();

	if (bUndoStable)
	{
		bPatchMounted = false;
		CurrentVersionString = UVersionRecord::GetBaseVersion();
		if (VersionManager)
		{
			VersionManager->SaveStableInfo(UVersionRecord::GetBaseVersion(), FString());
		}
	}

	const FString RemoteVersion = VersionManager ? VersionManager->GetRemoteVersion() : FString();
	if (VersionManager && !RemoteVersion.IsEmpty())
	{
		VersionManager->SaveLastFailedVersion(RemoteVersion);
	}

	bLastAttemptSucceeded = false;
	const FString Reverted = bUndoStable
		? CurrentVersionString
		: (VersionManager ? VersionManager->QueryLocalVersion() : FString(TEXT("<未知>")));

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 已回滚/稳定于版本 %s。"), *Reverted);
	OnUpdateRolledBack.Broadcast(Reverted);

	if (UpdateStateMachine)
	{
		UpdateStateMachine->TransitionTo(EHotUpdateState::Done);
	}
	RefreshUpdatePanel();
}

void UHotUpdateSubsystem::CleanupDownloadArtifacts()
{
	auto DeleteIfExists = [](const FString& Path)
	{
		if (!Path.IsEmpty() && IFileManager::Get().FileExists(*Path))
		{
			IFileManager::Get().Delete(*Path, false, true, true);
			UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 已删除文件：%s"), *Path);
		}
	};

	DeleteIfExists(TempPakPath);
	DeleteIfExists(DownloadedPakPath);
	DownloadedPakPath.Reset();
	TempPakPath.Reset();
}

void UHotUpdateSubsystem::LogStats() const
{
	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate][Stats] 补丁体积=%lld 字节"), LastStats.PakSize);
	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate][Stats] 下载耗时=%.2f 秒"), LastStats.DownloadSeconds);
	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate][Stats] 校验耗时=%.2f 秒"), LastStats.VerifySeconds);
	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate][Stats] 挂载耗时=%.2f 秒"), LastStats.MountSeconds);
	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate][Stats] 总耗时=%.2f 秒（尝试 %d 次）"),
		LastStats.TotalSeconds, LastStats.Attempts);
}

// ---------------------------------------------------------------------------
// UI 查询接口（Day 6）
// ---------------------------------------------------------------------------

FString UHotUpdateSubsystem::GetRemoteVersionString() const
{
	return VersionManager ? VersionManager->GetRemoteVersion() : FString();
}

FString UHotUpdateSubsystem::GetLastErrorText() const
{
	switch (LastError)
	{
	case EHotUpdateError::None:          return TEXT("无");
	case EHotUpdateError::Network:       return TEXT("网络异常（断网 / 连接失败 / HTTP 非 200）");
	case EHotUpdateError::ManifestParse: return TEXT("版本清单获取或解析失败");
	case EHotUpdateError::SizeMismatch:  return TEXT("文件大小不符");
	case EHotUpdateError::HashMismatch:  return TEXT("SHA-256 校验不符");
	case EHotUpdateError::DiskSpace:     return TEXT("磁盘空间不足");
	case EHotUpdateError::FileIO:        return TEXT("文件读写失败");
	case EHotUpdateError::MountFailed:   return TEXT("Pak 挂载失败（已回滚）");
	case EHotUpdateError::RollbackFailed:return TEXT("回滚失败");
	default:                             return TEXT("未知错误");
	}
}

FString UHotUpdateSubsystem::GetStatusText() const
{
	const EHotUpdateState State = GetUpdateState();
	const FString Local = VersionManager ? VersionManager->QueryLocalVersion() : FString(TEXT("?"));
	const FString Remote = GetRemoteVersionString();

	switch (State)
	{
	case EHotUpdateState::Idle:       return TEXT("就绪");
	case EHotUpdateState::Checking:   return TEXT("正在检查更新…");
	case EHotUpdateState::UpToDate:   return FString::Printf(TEXT("已是最新版本（%s）"), *Local);
	case EHotUpdateState::NeedUpdate: return FString::Printf(TEXT("发现新版本 %s（当前 %s）"), *Remote, *Local);
	case EHotUpdateState::ForceUpdate:
	{
		const FString LastFailed = VersionManager ? VersionManager->GetLastFailedVersion() : FString();
		if (!LastFailed.IsEmpty() && LastFailed == Remote)
		{
			return FString::Printf(TEXT("需要更新到 %s；上次更新失败，请手动重试"), *Remote);
		}
		return FString::Printf(TEXT("需要更新到 %s"), *Remote);
	}
	case EHotUpdateState::Downloading:
		return FString::Printf(TEXT("正在下载… %d%%"), FMath::RoundToInt(GetDownloadProgress() * 100.0f));
	case EHotUpdateState::Verifying:  return TEXT("正在校验文件…");
	case EHotUpdateState::Mounting:   return TEXT("正在挂载…");
	case EHotUpdateState::Done:
		return bLastAttemptSucceeded
			? FString::Printf(TEXT("更新完成（%s）"), *Local)
			: FString::Printf(TEXT("已回滚 / 保持版本 %s"), *Local);
	case EHotUpdateState::Failed:     return FString::Printf(TEXT("更新失败：%s"), *GetLastErrorText());
	case EHotUpdateState::RollingBack:return TEXT("更新失败，正在回滚…");
	default:                          return FString();
	}
}

float UHotUpdateSubsystem::GetDownloadProgress() const
{
	return DownloadBytesTotal > 0 ? float(double(DownloadBytesReceived) / double(DownloadBytesTotal)) : 0.0f;
}

// ---------------------------------------------------------------------------
// Slate 面板（Day 6）
// ---------------------------------------------------------------------------

void UHotUpdateSubsystem::HandleEngineLoopInitComplete()
{
	CreateUpdatePanel();
}

void UHotUpdateSubsystem::CreateUpdatePanel()
{
	if (UpdatePanel.IsValid())
	{
		return;
	}
	if (!GEngine || !GEngine->GameViewport)
	{
		// GameViewport 尚未就绪；等 OnFEngineLoopInitComplete 回调。
		return;
	}

	UpdatePanel = SNew(SUpdatePanel).Subsystem(this);
	GEngine->GameViewport->AddViewportWidgetContent(UpdatePanel.ToSharedRef(), 100);
	RefreshUpdatePanel();
	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] Slate 更新面板已加入视口。"));
}

void UHotUpdateSubsystem::DestroyUpdatePanel()
{
	if (UpdatePanel.IsValid())
	{
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(UpdatePanel.ToSharedRef());
		}
		UpdatePanel.Reset();
	}

	// 面板销毁后恢复默认输入模式。
	ApplyUpdateInputMode(false);
}

void UHotUpdateSubsystem::RefreshUpdatePanel()
{
	if (!UpdatePanel.IsValid())
	{
		return;
	}

	UpdatePanel->Refresh();

	// 面板可见时开鼠标、切 GameAndUI，便于点击按钮；隐藏时恢复。
	// 用面板实际可见性判断（面板内部可能因「跳过」而自行隐藏）。
	ApplyUpdateInputMode(UpdatePanel->GetVisibility().IsVisible());
}

void UHotUpdateSubsystem::ApplyUpdateInputMode(bool bUIActive)
{
	if (bUpdateInputModeApplied == bUIActive)
	{
		return;
	}

	APlayerController* PC = nullptr;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UWorld* World = GameInstance->GetWorld())
		{
			PC = World->GetFirstPlayerController();
		}
	}

	if (!PC)
	{
		// PlayerController 还没就绪：不改缓存，留待下次调用（面板 Tick 会重试）。
		return;
	}

	if (bUIActive)
	{
		PC->SetShowMouseCursor(true);
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
	}
	else
	{
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}

	bUpdateInputModeApplied = bUIActive;
}

void UHotUpdateSubsystem::HandleUpdateStateChanged(EHotUpdateState OldState, EHotUpdateState NewState)
{
	if (GEngine)
	{
		FColor Color = FColor::Green;
		if (NewState == EHotUpdateState::NeedUpdate || NewState == EHotUpdateState::ForceUpdate)
		{
			Color = FColor::Yellow;
		}
		else if (NewState == EHotUpdateState::Failed)
		{
			Color = FColor::Red;
		}
		else if (NewState == EHotUpdateState::Downloading || NewState == EHotUpdateState::Verifying
			|| NewState == EHotUpdateState::Mounting || NewState == EHotUpdateState::RollingBack)
		{
			Color = FColor::Cyan;
		}

		GEngine->AddOnScreenDebugMessage(-1, 6.0f, Color,
			FString::Printf(TEXT("HotUpdate 状态: %s"), *UUpdateStateMachine::GetStateDisplayName(NewState)));
	}

	OnUpdateStateChanged.Broadcast(OldState, NewState);
	RefreshUpdatePanel();
}
