// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SButton;
class SProgressBar;
class STextBlock;
class UHotUpdateSubsystem;

/**
 * 更新流程 Slate 面板（Day 6 / FR-10）。
 *
 * 纯 C++ Slate，不依赖任何 UMG 蓝图资产 → 基础包保持冻结。
 * 由 UHotUpdateSubsystem 创建并加入视口；子系统在状态/进度变化时调用 Refresh()
 * 主动刷新（避免 Slate 与动态多播委托的绑定限制）。
 */
class HOTPAKDEMO_API SUpdatePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUpdatePanel) {}
		/** 所属子系统（弱引用，避免生命周期问题）。 */
		SLATE_ARGUMENT(TWeakObjectPtr<UHotUpdateSubsystem>, Subsystem)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** 读取子系统当前状态并刷新控件。 */
	void Refresh();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	FReply HandleConfirmClicked();
	FReply HandleRetryClicked();
	FReply HandleDismissClicked();

	TWeakObjectPtr<UHotUpdateSubsystem> Subsystem;

	TSharedPtr<STextBlock> TitleText;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> RemainingText;
	TSharedPtr<SProgressBar> ProgressBar;
	TSharedPtr<SButton> ConfirmButton;
	TSharedPtr<SButton> RetryButton;
	TSharedPtr<SButton> DismissButton;

	/** 用户是否点了「跳过」。 */
	bool bDismissed = false;
};
