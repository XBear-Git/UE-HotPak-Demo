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

bool UVersionRecord::SaveVersion(const FString& NewVersion)
{
	if (NewVersion.IsEmpty())
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[HotUpdate] SaveVersion: 版本号为空，忽略写入。"));
		return false;
	}

	LocalVersion = NewVersion;
	const bool bSaved = UGameplayStatics::SaveGameToSlot(this, GetSlotName(), VersionRecordUserIndex);

	if (bSaved)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 本地版本已保存：%s（槽 %s）。"), *LocalVersion, *GetSlotName());
	}
	else
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] 本地版本保存失败：%s（槽 %s）。"), *LocalVersion, *GetSlotName());
	}

	return bSaved;
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
