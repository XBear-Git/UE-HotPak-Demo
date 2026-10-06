// Copyright Epic Games, Inc. All Rights Reserved.

#include "HotUpdateSubsystem.h"

#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "IPlatformFilePak.h"
#include "Misc/Paths.h"
#include "PakDownloader.h"
#include "PakMounter.h"
#include "PakVerifier.h"
#include "SHA256.h"
#include "VersionManager.h"

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

	// Day 3: create the update state machine and the version manager, wire the
	// state broadcast through to this subsystem, then run the first check.
	UpdateStateMachine = NewObject<UUpdateStateMachine>(this);
	VersionManager = NewObject<UVersionManager>(this);

	if (UpdateStateMachine && VersionManager)
	{
		UpdateStateMachine->OnStateChanged.AddDynamic(this, &UHotUpdateSubsystem::HandleUpdateStateChanged);
		// Day 4: react to the finished version check to route NeedUpdate / ForceUpdate.
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

	// Day 5: 启动时先重挂载本地已有补丁，再做版本检测——
	// 本地补丁在 Saved/HotUpdate 不会被引擎自动挂载，必须主动挂，才能保证重启后资源与版本一致。
	RemountLocalPatches();

	StartUpdateCheck();
}

void UHotUpdateSubsystem::Deinitialize()
{
	MountPakConsoleCommand.Reset();
	CheckForUpdateConsoleCommand.Reset();
	StatusConsoleCommand.Reset();
	ConfirmUpdateConsoleCommand.Reset();
	SelfTestConsoleCommand.Reset();
	Super::Deinitialize();
}

bool UHotUpdateSubsystem::MountPak(const FString& PakFilePath)
{
	EnsureMountPipeline();

	// 挂载本身交给 UPakMounter；子系统负责挂载成功后的资源生效通知。
	if (!PakMounter || !PakMounter->MountPak(PakFilePath, DefaultPatchPakOrder))
	{
		return false;
	}

	// 手动挂载成功后同样触发生效链；版本优先用最近检测到的远端版本。
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

void UHotUpdateSubsystem::HandleMountPakCommand(const TArray<FString>& Args)
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[HotUpdate] Usage: HotUpdate.MountPak <PakFilePath>"));
		return;
	}

	// Join in case the path contains spaces, then strip surrounding double quotes
	// (the UE console passes quotes through literally instead of stripping them).
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
	// 本地版本即时从存档读取；远端版本来自最近一次检测（由 CheckForUpdate 填充）。
	const FString LocalVersion = VersionManager
		? VersionManager->QueryLocalVersion()
		: FString(TEXT("<未初始化>"));
	const FString RemoteVersion = (VersionManager && !VersionManager->GetRemoteVersion().IsEmpty())
		? VersionManager->GetRemoteVersion()
		: FString(TEXT("<未知，尚未成功检测>"));
	const FString StateName = UUpdateStateMachine::GetStateDisplayName(GetUpdateState());

	const FString Message = FString::Printf(
		TEXT("HotUpdate 状态：本地版本=%s，远端版本=%s，当前状态=%s"),
		*LocalVersion, *RemoteVersion, *StateName);

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] %s"), *Message);

	// 打包版 Log 不上屏，这里用 AddOnScreenDebugMessage 给出可见反馈。
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Cyan, Message);
	}
}

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

void UHotUpdateSubsystem::HandleConfirmUpdateCommand(const TArray<FString>& Args)
{
	ConfirmUpdate();
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

void UHotUpdateSubsystem::HandleVersionCheckCompleted(EHotUpdateState ResultState, const FString& RemoteVersion)
{
	if (ResultState == EHotUpdateState::ForceUpdate)
	{
		// 强制更新：检测完成即自动下载，不等用户确认。
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 强制更新，自动开始下载。"));
		StartDownload();
	}
	else if (ResultState == EHotUpdateState::NeedUpdate)
	{
		// 普通更新：停在 NeedUpdate 等待确认（Day 4 用控制台命令模拟，Day 6 换成 UI）。
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 需更新，等待用户确认；执行 HotUpdate.ConfirmUpdate 开始下载。"));
	}
}

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
		if (UpdateStateMachine)
		{
			UpdateStateMachine->TransitionTo(EHotUpdateState::Failed);
		}
		return;
	}

	PendingPakInfo = Info;
	DownloadedPakPath.Reset();

	// 可写目录：Saved/HotUpdate。
	// 注意：不能用 Saved/Paks —— 引擎的 FPakPlatformFile::GetPakFolders() 会把
	// ProjectSavedDir/Paks 也加入 pak 扫描目录（ALL_PAKS_WILDCARD="*.pak"），
	// 下载进去的补丁会在下次启动被引擎自动挂载并占用文件句柄，导致改名覆盖失败，
	// 还会被引擎抢先挂载、绕过我们自己的 order/mount 逻辑。Saved/HotUpdate 不在扫描列表内。
	const FString PakDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HotUpdate"));
	IFileManager::Get().MakeDirectory(*PakDirectory, true);

	const FString FileName = FPaths::GetCleanFilename(PendingPakInfo.Url);
	TempPakPath = FPaths::Combine(PakDirectory, FileName + TEXT(".tmp"));
	DownloadAttempt = 0;

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 下载目录：%s"), *PakDirectory);

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

void UHotUpdateSubsystem::EnsureDownloadPipeline()
{
	if (!PakDownloader)
	{
		PakDownloader = NewObject<UPakDownloader>(this);
		PakDownloader->OnComplete.AddDynamic(this, &UHotUpdateSubsystem::HandlePakDownloadComplete);
	}
	if (!PakVerifier)
	{
		PakVerifier = NewObject<UPakVerifier>(this);
	}
}

void UHotUpdateSubsystem::BeginDownloadAttempt()
{
	++DownloadAttempt;

	if (UpdateStateMachine)
	{
		UpdateStateMachine->TransitionTo(EHotUpdateState::Downloading);
	}

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 第 %d/%d 次尝试：下载 %s"),
		DownloadAttempt, MaxDownloadAttempts, *PendingPakInfo.Url);

	PakDownloader->StartDownload(PendingPakInfo.Url, TempPakPath);
}

void UHotUpdateSubsystem::CleanupTempPakFile()
{
	if (TempPakPath.IsEmpty())
	{
		return;
	}

	if (IFileManager::Get().FileExists(*TempPakPath))
	{
		IFileManager::Get().Delete(*TempPakPath, false, true, true);
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 已删除临时文件：%s"), *TempPakPath);
	}
}

void UHotUpdateSubsystem::HandleDownloadAttemptFailed(const FString& Reason)
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
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] 重试次数耗尽（共 %d 次），进入 Failed。"), MaxDownloadAttempts);
		if (UpdateStateMachine)
		{
			UpdateStateMachine->TransitionTo(EHotUpdateState::Failed);
		}
	}
}

void UHotUpdateSubsystem::HandlePakDownloadComplete(bool bSuccess, const FString& LocalPath)
{
	if (!bSuccess)
	{
		HandleDownloadAttemptFailed(TEXT("下载失败"));
		return;
	}

	if (UpdateStateMachine)
	{
		UpdateStateMachine->TransitionTo(EHotUpdateState::Verifying);
	}

	const EPakVerifyResult Result = PakVerifier
		? PakVerifier->Verify(LocalPath, PendingPakInfo.Sha256, PendingPakInfo.Size)
		: EPakVerifyResult::ReadError;

	if (Result == EPakVerifyResult::Success)
	{
		FinalizeDownloadedPak();
		return;
	}

	HandleDownloadAttemptFailed(UPakVerifier::GetResultDisplayName(Result));
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
		HandleDownloadAttemptFailed(TEXT("改名为 .pak 失败"));
		return;
	}

	DownloadedPakPath = FinalPath;
	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 校验通过，已改名为正式 Pak：%s"), *FinalPath);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Green,
			FString::Printf(TEXT("热更包已下载并校验通过：%s"), *FPaths::GetCleanFilename(FinalPath)));
	}

	// Day 5：改名成功后进入挂载 → 生效 → 持久化。
	if (UpdateStateMachine)
	{
		UpdateStateMachine->TransitionTo(EHotUpdateState::Mounting);
	}
	MountDownloadedPak();
}

void UHotUpdateSubsystem::EnsureMountPipeline()
{
	if (!PakMounter)
	{
		PakMounter = NewObject<UPakMounter>(this);
	}
}

void UHotUpdateSubsystem::RemountLocalPatches()
{
	EnsureMountPipeline();

	const FString PatchDirectory = UPakMounter::GetLocalPatchDirectory();
	TArray<FString> PakFiles;
	IFileManager::Get().FindFiles(PakFiles, *(FPaths::Combine(PatchDirectory, TEXT("*.pak"))), true, false);

	if (PakFiles.Num() == 0)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 启动重挂载：本地无补丁（%s）。"), *PatchDirectory);
		return;
	}

	// 文件名排序，保证挂载顺序稳定（多补丁排序是 Day 6 的事）。
	PakFiles.Sort();

	int32 MountedCount = 0;
	for (const FString& PakFileName : PakFiles)
	{
		const FString PakPath = FPaths::Combine(PatchDirectory, PakFileName);
		if (PakMounter->MountPak(PakPath, DefaultPatchPakOrder))
		{
			++MountedCount;
		}
	}

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 启动重挂载完成：%d/%d 个本地补丁已挂载。"),
		MountedCount, PakFiles.Num());

	// 挂了本地补丁后，视为「热更已应用」：
	// 设置 bPatchMounted 并更新版本，使随后的 ABaseHotTestActor::BeginPlay
	// 触发 OnHotfixApplied()，让测试场景/UI 重新读取（补丁里的）资源。
	// 否则资源文件虽已是新版，但 UI 刷新链路不会运行（贴图这类自动采样的资源不受影响）。
	if (MountedCount > 0)
	{
		const FString LocalVersion = VersionManager ? VersionManager->QueryLocalVersion() : FString();
		NotifyHotfixApplied(LocalVersion);
	}
}

void UHotUpdateSubsystem::MountDownloadedPak()
{
	if (DownloadedPakPath.IsEmpty())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] MountDownloadedPak: 本地 Pak 路径为空，无法挂载。"));
		if (UpdateStateMachine)
		{
			UpdateStateMachine->TransitionTo(EHotUpdateState::Failed);
		}
		return;
	}

	EnsureMountPipeline();

	const int32 MountOrder = (PendingPakInfo.Order > 0) ? PendingPakInfo.Order : DefaultPatchPakOrder;
	if (!PakMounter->MountPak(DownloadedPakPath, MountOrder))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] 挂载下载的 Pak 失败：%s"), *DownloadedPakPath);
		if (UpdateStateMachine)
		{
			UpdateStateMachine->TransitionTo(EHotUpdateState::Failed);
		}
		return;
	}

	// 资源生效链：通知测试场景重载资源（Day 1 已有链路）。
	const FString RemoteVersion = VersionManager ? VersionManager->GetRemoteVersion() : FString();
	NotifyHotfixApplied(RemoteVersion);

	// FR-08 唯一写入点：挂载成功后持久化本地版本。
	if (VersionManager && !RemoteVersion.IsEmpty())
	{
		VersionManager->SaveLocalVersion(RemoteVersion);
	}

	if (UpdateStateMachine)
	{
		UpdateStateMachine->TransitionTo(EHotUpdateState::Done);
	}
}

void UHotUpdateSubsystem::HandleUpdateStateChanged(EHotUpdateState OldState, EHotUpdateState NewState)
{
	// Log-level messages don't show on screen in a packaged build, so surface the
	// state change with a colored on-screen message as well (see Day 1 pitfall #3).
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
		else if (NewState == EHotUpdateState::Downloading || NewState == EHotUpdateState::Verifying)
		{
			Color = FColor::Cyan;
		}

		GEngine->AddOnScreenDebugMessage(-1, 6.0f, Color,
			FString::Printf(TEXT("HotUpdate 状态: %s"), *UUpdateStateMachine::GetStateDisplayName(NewState)));
	}

	OnUpdateStateChanged.Broadcast(OldState, NewState);
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
