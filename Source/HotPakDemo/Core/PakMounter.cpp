// Copyright Epic Games, Inc. All Rights Reserved.

#include "PakMounter.h"

#include "Engine/Engine.h"
#include "HAL/PlatformFileManager.h"
#include "IPlatformFilePak.h"
#include "Misc/Paths.h"

#include "../HotPakDemo.h"

namespace
{
	/** 返回当前平台文件链上的 Pak 层；只在打包版（或 -pak）存在，编辑器里为 null。 */
	FPakPlatformFile* FindPakPlatformFile()
	{
		return static_cast<FPakPlatformFile*>(
			FPlatformFileManager::Get().FindPlatformFile(FPakPlatformFile::GetTypeName()));
	}
}

FString UPakMounter::GetLocalPatchDirectory()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HotUpdate"));
}

bool UPakMounter::MountPak(const FString& PakFilePath, int32 PakOrder)
{
	if (PakFilePath.IsEmpty())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate][Mount] 空的 Pak 路径。"));
		return false;
	}

	FPakPlatformFile* PakPlatformFile = FindPakPlatformFile();
	if (!PakPlatformFile)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate][Mount] FPakPlatformFile 不可用（是否在打包版运行？）。"));
		return false;
	}

	// nullptr 挂载点 = 使用 Pak 自带的挂载点自动对齐；order 高于基础包(0) 使补丁资源优先。
	const uint32 Order = uint32(FMath::Max(PakOrder, 0));
	if (!PakPlatformFile->Mount(*PakFilePath, Order, nullptr))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate][Mount] 挂载失败：%s（文件无效、缺密钥或已挂载？）。"), *PakFilePath);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.0f, FColor::Red,
				FString::Printf(TEXT("HotUpdate: 挂载失败 %s"), *FPaths::GetCleanFilename(PakFilePath)));
		}
		return false;
	}

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate][Mount] 挂载成功：%s（order=%d）。"), *PakFilePath, Order);

	// Log 级别在打包版不上屏，用 AddOnScreenDebugMessage 给出可见反馈。
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.0f, FColor::Green,
			FString::Printf(TEXT("HotUpdate: 挂载成功 %s"), *FPaths::GetCleanFilename(PakFilePath)));
	}

	TArray<FString> MountedPaks;
	PakPlatformFile->GetMountedPakFilenames(MountedPaks);
	for (const FString& MountedPak : MountedPaks)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate][Mount] 已挂载 Pak -> %s"), *MountedPak);
	}

	return true;
}

bool UPakMounter::UnmountPak(const FString& PakFilePath)
{
	FPakPlatformFile* PakPlatformFile = FindPakPlatformFile();
	if (!PakPlatformFile)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate][Mount] Unmount: FPakPlatformFile 不可用。"));
		return false;
	}

	const bool bUnmounted = PakPlatformFile->Unmount(*PakFilePath);
	if (bUnmounted)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate][Mount] 卸载成功：%s"), *PakFilePath);
	}
	else
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[HotUpdate][Mount] 卸载失败：%s"), *PakFilePath);
	}
	return bUnmounted;
}

TArray<FString> UPakMounter::GetMountedPakFilenames() const
{
	TArray<FString> Result;

	if (FPakPlatformFile* PakPlatformFile = FindPakPlatformFile())
	{
		PakPlatformFile->GetMountedPakFilenames(Result);
	}

	return Result;
}
