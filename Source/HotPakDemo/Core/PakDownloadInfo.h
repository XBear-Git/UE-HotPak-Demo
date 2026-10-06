// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PakDownloadInfo.generated.h"

/**
 * 远端 Pak 的下载信息（对应 version.json 的 `pak` 字段，Day 4）。
 *
 * 由 UVersionManager 解析并持有，供 UPakDownloader / UPakVerifier / （Day 5）挂载器共用。
 */
USTRUCT(BlueprintType)
struct HOTPAKDEMO_API FPakDownloadInfo
{
	GENERATED_BODY()

	/** 增量 Pak 的下载地址。 */
	UPROPERTY(BlueprintReadWrite, Category = "Hot Update|Download")
	FString Url;

	/** 期望文件大小（字节），用于下载后快速校验。 */
	UPROPERTY(BlueprintReadWrite, Category = "Hot Update|Download")
	int64 Size = 0;

	/** 期望 SHA-256（64 位小写十六进制）。 */
	UPROPERTY(BlueprintReadWrite, Category = "Hot Update|Download")
	FString Sha256;

	/** 挂载优先级（补丁高于基础包）。 */
	UPROPERTY(BlueprintReadWrite, Category = "Hot Update|Download")
	int32 Order = 100;

	/** Pak 内挂载点。 */
	UPROPERTY(BlueprintReadWrite, Category = "Hot Update|Download")
	FString MountPoint;

	/** 是否已解析到可用的下载地址。 */
	bool IsValid() const { return !Url.IsEmpty(); }
};
