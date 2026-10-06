// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "VersionRecord.generated.h"

/**
 * 本地版本记录（FR-08 + Day 6 回滚）。
 *
 * 用 SaveGame 槽持久化客户端的稳定版本状态，作为下次启动比对远端清单的基线。
 * 槽名固定为 "HotUpdateVersion"。
 *
 * Day 6 扩展：
 * - StablePakFileName：当前稳定补丁文件名（启动只挂它，避免多补丁 order 冲突）；
 * - LastFailedVersion：上次更新失败的远端版本（防 ForceUpdate 跨重启死循环）。
 */
UCLASS(BlueprintType)
class HOTPAKDEMO_API UVersionRecord : public USaveGame
{
	GENERATED_BODY()

public:
	/** 本地稳定版本号，默认 1.0.0（与基础包一致）。 */
	UPROPERTY(BlueprintReadWrite, Category = "Hot Update|Persistence")
	FString LocalVersion = TEXT("1.0.0");

	/** 稳定补丁文件名（仅文件名，位于 Saved/HotUpdate）。为空表示当前为基础版。 */
	UPROPERTY(BlueprintReadWrite, Category = "Hot Update|Persistence")
	FString StablePakFileName;

	/** 上次更新失败的版本号（用于避免 ForceUpdate 反复自动下载同一个坏包）。 */
	UPROPERTY(BlueprintReadWrite, Category = "Hot Update|Persistence")
	FString LastFailedVersion;

	/** 固定存档槽名。 */
	static const FString& GetSlotName();

	/** 基础版本号（具名常量，避免硬编码散落）。 */
	static FString GetBaseVersion();

	/** 读取现有存档并返回；不存在或损坏时返回一个新的、以基础版初始化的记录。 */
	static UVersionRecord* LoadOrCreateRecord();

	/** 将当前记录写入存档槽。 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|Persistence")
	bool SaveToSlot();

	/** 将版本号写入本地存档（保留其它字段）。 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|Persistence")
	bool SaveVersion(const FString& NewVersion);

	/** 挂载成功后写入稳定版本 + 稳定补丁文件名（并清除失败记录）。 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|Persistence")
	bool SaveStableInfo(const FString& InVersion, const FString& InPakFileName);

	/** 记录一次失败的版本（用于防 ForceUpdate 死循环）。 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|Persistence")
	bool SaveLastFailedVersion(const FString& InVersion);

	/**
	 * 读取本地存档中的版本号。
	 * 存档不存在或损坏时返回 DefaultVersion。
	 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|Persistence")
	static FString LoadVersion(const FString& DefaultVersion);
};
