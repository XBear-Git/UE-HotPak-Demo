// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ST_HotfixConfig.generated.h"

/**
 * DataTable row used by the hot-update verification scene.
 *
 * The base package and patch package can use the same row name with different
 * values to verify that a mounted Pak changes gameplay data and UI together.
 */
USTRUCT(BlueprintType)
struct HOTPAKDEMO_API FST_HotfixConfig : public FTableRowBase
{
	GENERATED_BODY()

	/** 当前资源版本号，显示在 UI 左上角。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotfix Config")
	FString VersionTag = TEXT("1.0.0");

	/** 得分倍率，影响 HUD 或测试分数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotfix Config")
	float ScoreMultiplier = 1.0f;

	/** 最大生命值，影响血条或数值显示。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotfix Config")
	int32 MaxHealth = 100;

	/** 主标题文字，显示在 UI 中央。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotfix Config")
	FText TitleText = FText::FromString(TEXT("基础版本"));

	/** 功能开关，控制某个测试功能是否启用。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotfix Config")
	bool EnableFeatureX = false;

	/** 主题色，改变 UI 或场景灯光颜色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotfix Config")
	FLinearColor ColorPreset = FLinearColor::White;

	/** 备用整数参数，可用于积分、等级等测试。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotfix Config")
	int32 ExtraIntParam = 0;

	/** 备用浮点参数，可用于冷却时间、速度等测试。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotfix Config")
	float ExtraFloatParam = 0.0f;
};
