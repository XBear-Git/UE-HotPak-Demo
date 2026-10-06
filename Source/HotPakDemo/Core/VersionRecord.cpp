// Copyright Epic Games, Inc. All Rights Reserved.

#include "VersionRecord.h"

#include "Kismet/GameplayStatics.h"

#include "../HotPakDemo.h"

namespace
{
	// 存档用户索引，单机测试固定 0。
	constexpr int32 VersionRecordUserIndex = 0;
}

const FString& UVersionRecord::GetSlotName()
{
	static const FString SlotName = TEXT("HotUpdateVersion");
	return SlotName;
}

FString UVersionRecord::GetBaseVersion()
{
	return TEXT("1.0.0");
}

UVersionRecord* UVersionRecord::LoadOrCreateRecord()
{
	if (UGameplayStatics::DoesSaveGameExist(GetSlotName(), VersionRecordUserIndex))
	{
		if (UVersionRecord* Loaded = Cast<UVersionRecord>(
			UGameplayStatics::LoadGameFromSlot(GetSlotName(), VersionRecordUserIndex)))
		{
			if (Loaded->LocalVersion.IsEmpty())
			{
				Loaded->LocalVersion = GetBaseVersion();
			}
			return Loaded;
		}
	}

	UVersionRecord* NewRecord = NewObject<UVersionRecord>();
	NewRecord->LocalVersion = GetBaseVersion();
	return NewRecord;
}

bool UVersionRecord::SaveToSlot()
{
	const bool bSaved = UGameplayStatics::SaveGameToSlot(this, GetSlotName(), VersionRecordUserIndex);
	if (bSaved)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 本地记录已保存：version=%s，stablePak=%s，lastFailed=%s。"),
			*LocalVersion,
			StablePakFileName.IsEmpty() ? TEXT("<无>") : *StablePakFileName,
			LastFailedVersion.IsEmpty() ? TEXT("<无>") : *LastFailedVersion);
	}
	else
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] 本地记录保存失败（槽 %s）。"), *GetSlotName());
	}
	return bSaved;
}

bool UVersionRecord::SaveVersion(const FString& NewVersion)
{
	if (NewVersion.IsEmpty())
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[HotUpdate] SaveVersion: 版本号为空，忽略写入。"));
		return false;
	}

	LocalVersion = NewVersion;
	return SaveToSlot();
}

bool UVersionRecord::SaveStableInfo(const FString& InVersion, const FString& InPakFileName)
{
	LocalVersion = InVersion.IsEmpty() ? GetBaseVersion() : InVersion;
	StablePakFileName = InPakFileName;
	// 更新成功即清除失败记录。
	LastFailedVersion.Reset();
	return SaveToSlot();
}

bool UVersionRecord::SaveLastFailedVersion(const FString& InVersion)
{
	LastFailedVersion = InVersion;
	return SaveToSlot();
}

FString UVersionRecord::LoadVersion(const FString& DefaultVersion)
{
	if (!UGameplayStatics::DoesSaveGameExist(GetSlotName(), VersionRecordUserIndex))
	{
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 未找到本地版本记录，使用默认版本 %s。"), *DefaultVersion);
		return DefaultVersion;
	}

	UVersionRecord* LoadedRecord = Cast<UVersionRecord>(
		UGameplayStatics::LoadGameFromSlot(GetSlotName(), VersionRecordUserIndex));

	if (!LoadedRecord || LoadedRecord->LocalVersion.IsEmpty())
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[HotUpdate] 本地版本记录损坏，使用默认版本 %s。"), *DefaultVersion);
		return DefaultVersion;
	}

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 读取本地版本：%s。"), *LoadedRecord->LocalVersion);
	return LoadedRecord->LocalVersion;
}
