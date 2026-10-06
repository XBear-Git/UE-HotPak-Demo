// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HotUpdateTypes.generated.h"

/**
 * 更新流程的错误原因（Day 6，便于日志区分与 UI 文案）。
 */
UENUM(BlueprintType)
enum class EHotUpdateError : uint8
{
	None          UMETA(DisplayName = "None"),          // 无错误
	Network       UMETA(DisplayName = "Network"),        // 断网 / 连接失败 / HTTP 非 200
	ManifestParse UMETA(DisplayName = "ManifestParse"),  // 清单拉取或解析失败 / 缺字段
	SizeMismatch  UMETA(DisplayName = "SizeMismatch"),   // 文件大小不符
	HashMismatch  UMETA(DisplayName = "HashMismatch"),   // SHA-256 不符
	DiskSpace     UMETA(DisplayName = "DiskSpace"),      // 磁盘空间不足
	FileIO        UMETA(DisplayName = "FileIO"),         // 写入 / 改名失败
	MountFailed   UMETA(DisplayName = "MountFailed"),    // 挂载失败（触发实质回滚）
	RollbackFailed UMETA(DisplayName = "RollbackFailed") // 回滚本身失败
};

/** 关键节点耗时与体积统计（Day 6）。 */
USTRUCT(BlueprintType)
struct HOTPAKDEMO_API FHotUpdateStats
{
	GENERATED_BODY()

	/** 补丁体积（字节）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Hot Update|Stats")
	int64 PakSize = 0;

	/** 下载耗时（秒）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Hot Update|Stats")
	double DownloadSeconds = 0.0;

	/** 校验耗时（秒）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Hot Update|Stats")
	double VerifySeconds = 0.0;

	/** 挂载耗时（秒）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Hot Update|Stats")
	double MountSeconds = 0.0;

	/** 本次更新流程总耗时（秒）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Hot Update|Stats")
	double TotalSeconds = 0.0;

	/** 下载+校验的尝试次数。 */
	UPROPERTY(BlueprintReadOnly, Category = "Hot Update|Stats")
	int32 Attempts = 0;
};
