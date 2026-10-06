// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PakMounter.generated.h"

/**
 * Pak 挂载 / 卸载封装（Day 5 / FR-07）。
 *
 * 由 UHotUpdateSubsystem 持有，只负责「挂载本身」：不广播、不写版本——
 * 资源生效（NotifyHotfixApplied）与本地版本持久化由子系统编排。
 */
UCLASS(BlueprintType)
class HOTPAKDEMO_API UPakMounter : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 挂载一个 Pak。
	 * @param PakFilePath 绝对路径。
	 * @param PakOrder    挂载优先级，默认 100（高于基础包 order=0，使补丁资源优先）。
	 * 传 nullptr 作为挂载点，让引擎使用 Pak 自带的挂载点自动对齐（Day 1 已验证）。
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|Mount")
	bool MountPak(const FString& PakFilePath, int32 PakOrder = 100);

	/** 卸载一个已挂载的 Pak（Day 6 回滚时使用）。 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|Mount")
	bool UnmountPak(const FString& PakFilePath);

	/** 返回当前已挂载的 Pak 文件名列表（用于验证挂载结果）。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|Mount")
	TArray<FString> GetMountedPakFilenames() const;

	/**
	 * 本地补丁目录：`Saved/HotUpdate`。
	 * 注意不能用 `Saved/Paks`——那是引擎自动扫描并挂载的目录（Day 4 踩坑）。
	 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|Mount")
	static FString GetLocalPatchDirectory();
};
