// Copyright Epic Games, Inc. All Rights Reserved.

#include "UpdatePanel.h"

#include "HotUpdateSubsystem.h"
#include "UpdateStateMachine.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

void SUpdatePanel::Construct(const FArguments& InArgs)
{
	Subsystem = InArgs._Subsystem;
	bDismissed = false;

	ChildSlot
	[
		SNew(SBox)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(24.0f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(16.0f))
			[
				SNew(SBox)
				.WidthOverride(320.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SAssignNew(TitleText, STextBlock)
						.Text(FText::FromString(TEXT("资源更新")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SAssignNew(StatusText, STextBlock)
						.Text(FText::GetEmpty())
						.AutoWrapText(true)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
					[
						SAssignNew(ProgressBar, SProgressBar)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SAssignNew(RemainingText, STextBlock)
						.Text(FText::GetEmpty())
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
						[
							SAssignNew(ConfirmButton, SButton)
							.Text(FText::FromString(TEXT("更新")))
							.OnClicked(this, &SUpdatePanel::HandleConfirmClicked)
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
						[
							SAssignNew(RetryButton, SButton)
							.Text(FText::FromString(TEXT("重试")))
							.OnClicked(this, &SUpdatePanel::HandleRetryClicked)
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
						[
							SAssignNew(DismissButton, SButton)
							.Text(FText::FromString(TEXT("跳过")))
							.OnClicked(this, &SUpdatePanel::HandleDismissClicked)
						]
					]
				]
			]
		]
	];

	Refresh();
}

void SUpdatePanel::Refresh()
{
	UHotUpdateSubsystem* Sub = Subsystem.Get();
	if (!Sub)
	{
		SetVisibility(EVisibility::Collapsed);
		return;
	}

	const EHotUpdateState State = Sub->GetUpdateState();

	// 新一轮检测开始时重置「跳过」。
	if (State == EHotUpdateState::Checking)
	{
		bDismissed = false;
	}

	const bool bShouldShow = !bDismissed
		&& State != EHotUpdateState::Idle
		&& State != EHotUpdateState::UpToDate;

	SetVisibility(bShouldShow ? EVisibility::Visible : EVisibility::Collapsed);

	// 面板可见性变化时同步鼠标/输入模式（覆盖「跳过」等所有隐藏路径）。
	Sub->ApplyUpdateInputMode(bShouldShow);

	if (!bShouldShow)
	{
		return;
	}

	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(Sub->GetStatusText()));
	}

	// 进度条 + 剩余大小：仅下载中显示。
	const bool bDownloading = (State == EHotUpdateState::Downloading);
	if (ProgressBar.IsValid())
	{
		ProgressBar->SetVisibility(bDownloading ? EVisibility::Visible : EVisibility::Collapsed);
		if (bDownloading)
		{
			ProgressBar->SetPercent(TOptional<float>(Sub->GetDownloadProgress()));
		}
	}
	if (RemainingText.IsValid())
	{
		RemainingText->SetVisibility(bDownloading ? EVisibility::Visible : EVisibility::Collapsed);
		if (bDownloading)
		{
			const int64 Received = Sub->GetDownloadBytesReceived();
			const int64 Total = Sub->GetDownloadBytesTotal();
			const double RemainingMB = FMath::Max<double>(0.0, double(Total - Received)) / (1024.0 * 1024.0);
			RemainingText->SetText(FText::FromString(FString::Printf(
				TEXT("%.2f / %.2f MB（剩余 %.2f MB）"),
				double(Received) / (1024.0 * 1024.0), double(Total) / (1024.0 * 1024.0), RemainingMB)));
		}
	}

	// 按钮可见性。
	const bool bNeedUpdate = (State == EHotUpdateState::NeedUpdate || State == EHotUpdateState::ForceUpdate);
	const bool bShowRetry = (State == EHotUpdateState::Failed)
		|| (State == EHotUpdateState::Done && !Sub->IsPatchMounted());

	if (ConfirmButton.IsValid())
	{
		ConfirmButton->SetVisibility(bNeedUpdate ? EVisibility::Visible : EVisibility::Collapsed);
	}
	if (RetryButton.IsValid())
	{
		RetryButton->SetVisibility(bShowRetry ? EVisibility::Visible : EVisibility::Collapsed);
	}
	if (DismissButton.IsValid())
	{
		DismissButton->SetVisibility(State == EHotUpdateState::NeedUpdate ? EVisibility::Visible : EVisibility::Collapsed);
	}
}

void SUpdatePanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// PlayerController 可能在面板创建后才就绪；可见时每次 Tick 都确保输入模式（幂等）。
	if (GetVisibility().IsVisible())
	{
		if (UHotUpdateSubsystem* Sub = Subsystem.Get())
		{
			Sub->ApplyUpdateInputMode(true);
		}
	}
}

FReply SUpdatePanel::HandleConfirmClicked()
{
	if (UHotUpdateSubsystem* Sub = Subsystem.Get())
	{
		Sub->ConfirmUpdate();
	}
	return FReply::Handled();
}

FReply SUpdatePanel::HandleRetryClicked()
{
	if (UHotUpdateSubsystem* Sub = Subsystem.Get())
	{
		Sub->RetryUpdate();
	}
	return FReply::Handled();
}

FReply SUpdatePanel::HandleDismissClicked()
{
	bDismissed = true;
	Refresh();
	return FReply::Handled();
}
