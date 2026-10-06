// Copyright Epic Games, Inc. All Rights Reserved.

#include "PakVerifier.h"

#include "HAL/PlatformFileManager.h"
#include "SHA256.h"

#include "../HotPakDemo.h"

EPakVerifyResult UPakVerifier::Verify(const FString& FilePath, const FString& ExpectedSha256, int64 ExpectedSize)
{
	LastActualHash.Reset();

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (!PlatformFile.FileExists(*FilePath))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate][Verify] 文件不存在：%s"), *FilePath);
		return EPakVerifyResult::FileNotFound;
	}

	// 先比大小：快，且能省去对明显错误文件做哈希。
	if (ExpectedSize > 0)
	{
		const int64 ActualSize = PlatformFile.FileSize(*FilePath);
		if (ActualSize != ExpectedSize)
		{
			UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate][Verify] 大小不符：实际=%lld，期望=%lld（%s）"),
				ActualSize, ExpectedSize, *FilePath);
			return EPakVerifyResult::SizeMismatch;
		}
	}

	FString ActualHash;
	if (!HotUpdateSha256::HashFile(FilePath, ActualHash))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate][Verify] 读取文件失败：%s"), *FilePath);
		return EPakVerifyResult::ReadError;
	}
	LastActualHash = ActualHash;

	if (!ExpectedSha256.IsEmpty() && !ActualHash.Equals(ExpectedSha256, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate][Verify] SHA-256 不符：实际=%s，期望=%s"),
			*ActualHash, *ExpectedSha256);
		return EPakVerifyResult::HashMismatch;
	}

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate][Verify] 校验通过：%s（大小=%lld，SHA-256=%s）"),
		*FilePath, ExpectedSize, *ActualHash);
	return EPakVerifyResult::Success;
}

FString UPakVerifier::GetResultDisplayName(EPakVerifyResult Result)
{
	switch (Result)
	{
	case EPakVerifyResult::Success:      return TEXT("校验通过");
	case EPakVerifyResult::FileNotFound: return TEXT("文件不存在");
	case EPakVerifyResult::SizeMismatch: return TEXT("文件大小不符");
	case EPakVerifyResult::HashMismatch: return TEXT("SHA-256 不符");
	case EPakVerifyResult::ReadError:    return TEXT("文件读取失败");
	default:                             return TEXT("未知结果");
	}
}
