// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "VersionRecord.generated.h"

/**
 * 本地版本记录（对应 FR-08）。
 *
 * 用 SaveGame 槽持久化客户端当前资源版本，作为下次启动比对远端清单的基线。
 * 槽名固定为 "HotUpdateVersion"，避免与其它存档竞争。
 *
 * 注意：真正写入本地版本的时机在 Day 5「挂载成功后」；Day 3 先实现读写接口，
 * 并在判定为「已最新」时写入远端版本，用于跑通验证分支。
 */
UCLASS(BlueprintType)
class HOTPAKDEMO_API UVersionRecord : public USaveGame
{
	GENERATED_BODY()

public:
	/** 本地当前版本号，默认 1.0.0（与基础包一致）。 */
	UPROPERTY(BlueprintReadWrite, Category = "Hot Update|Persistence")
	FString LocalVersion = TEXT("1.0.0");

	/** 固定存档槽名。 */
	static const FString& GetSlotName();

	/** 将 NewVersion 写入本地存档，成功返回 true；空字符串直接失败。 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|Persistence")
	bool SaveVersion(const FString& NewVersion);

	/**
	 * 读取本地存档中的版本号。
	 * 存档不存在或损坏时返回 DefaultVersion。
	 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|Persistence")
	static FString LoadVersion(const FString& DefaultVersion);
};
