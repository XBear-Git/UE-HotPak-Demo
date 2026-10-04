// Copyright Epic Games, Inc. All Rights Reserved.

#include "HotUpdateSubsystem.h"

#include "Engine/Engine.h"
#include "HAL/PlatformFileManager.h"
#include "IPlatformFilePak.h"
#include "Misc/Paths.h"
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

	// Day 3: create the update state machine and the version manager, wire the
	// state broadcast through to this subsystem, then run the first check.
	UpdateStateMachine = NewObject<UUpdateStateMachine>(this);
	VersionManager = NewObject<UVersionManager>(this);

	if (UpdateStateMachine && VersionManager)
	{
		UpdateStateMachine->OnStateChanged.AddDynamic(this, &UHotUpdateSubsystem::HandleUpdateStateChanged);
		VersionManager->Initialize(UpdateStateMachine);
	}

	StartUpdateCheck();
}

void UHotUpdateSubsystem::Deinitialize()
{
	MountPakConsoleCommand.Reset();
	CheckForUpdateConsoleCommand.Reset();
	StatusConsoleCommand.Reset();
	Super::Deinitialize();
}

bool UHotUpdateSubsystem::MountPak(const FString& PakFilePath)
{
	if (PakFilePath.IsEmpty())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] MountPak: empty pak path."));
		return false;
	}

	// Locate the pak platform file layer. It only exists in a packaged build
	// (or when -pak is passed), so this is a meaningful guard for the POC.
	FPakPlatformFile* PakPlatformFile = static_cast<FPakPlatformFile*>(
		FPlatformFileManager::Get().FindPlatformFile(FPakPlatformFile::GetTypeName()));
	if (!PakPlatformFile)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] MountPak: FPakPlatformFile unavailable (are you in a packaged build?)."));
		return false;
	}

	// Pass nullptr as the mount point so Mount() uses the pak's own recorded
	// mount point (verified against FPakPlatformFile::Mount source). PakOrder 100
	// places this pak above the base pak (order 0), so its assets win lookups.
	constexpr uint32 PatchPakOrder = 100;
	if (!PakPlatformFile->Mount(*PakFilePath, PatchPakOrder, nullptr))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] MountPak: failed to mount '%s' (invalid file, missing key, or already mounted?)."), *PakFilePath);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.0f, FColor::Red,
				FString::Printf(TEXT("HotUpdate: 挂载失败 %s"), *FPaths::GetCleanFilename(PakFilePath)));
		}
		return false;
	}

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] MountPak: mounted '%s' (order=%d)."), *PakFilePath, PatchPakOrder);

	// Immediate on-screen feedback: Log-level messages don't show on screen in a
	// packaged build, so use AddOnScreenDebugMessage for a visible confirmation.
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.0f, FColor::Green,
			FString::Printf(TEXT("HotUpdate: 挂载成功 %s"), *FPaths::GetCleanFilename(PakFilePath)));
	}

	// Log the mounted pak list so the mount can be verified in the output log.
	TArray<FString> MountedPaks;
	PakPlatformFile->GetMountedPakFilenames(MountedPaks);
	for (const FString& MountedPak : MountedPaks)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] MountPak: mounted pak -> %s"), *MountedPak);
	}

	// POC hook: notify observers (e.g. ABaseHotTestActor) through the existing
	// callback contract. The real version string will come from the remote
	// manifest (Day 3), and rollback from a failed mount is added later (Day 6).
	NotifyHotfixApplied(TEXT("1.1.0"));
	return true;
}

TArray<FString> UHotUpdateSubsystem::GetMountedPakFilenames() const
{
	TArray<FString> Result;

	if (FPakPlatformFile* PakPlatformFile = static_cast<FPakPlatformFile*>(
		FPlatformFileManager::Get().FindPlatformFile(FPakPlatformFile::GetTypeName())))
	{
		PakPlatformFile->GetMountedPakFilenames(Result);
	}

	return Result;
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
