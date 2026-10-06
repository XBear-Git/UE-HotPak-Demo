// Copyright Epic Games, Inc. All Rights Reserved.

#include "PakDownloader.h"

#include "HttpModule.h"
#include "Misc/FileHelper.h"

#include "../HotPakDemo.h"

void UPakDownloader::StartDownload(const FString& InUrl, const FString& InSavePath)
{
	CurrentUrl = InUrl;
	CurrentSavePath = InSavePath;
	BytesReceived = 0;
	TotalBytes = 0;
	LastLoggedPercent = -1;
	bCompleteBroadcast = false;

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate][Download] 开始下载：%s -> %s"), *InUrl, *InSavePath);

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(InUrl);
	Request->SetVerb(TEXT("GET"));
	// BindUObject 使用弱引用，UPakDownloader 销毁后回调不会悬空。
	Request->OnRequestProgress64().BindUObject(this, &UPakDownloader::HandleProgress);
	Request->OnProcessRequestComplete().BindUObject(this, &UPakDownloader::HandleCompletion);

	if (!Request->ProcessRequest())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate][Download] 请求发起失败：%s"), *InUrl);
		bCompleteBroadcast = true;
		OnComplete.Broadcast(false, CurrentSavePath);
	}
}

void UPakDownloader::HandleProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived64)
{
	BytesReceived = int64(BytesReceived64);

	// 已知总大小时按 10% 粒度打日志，避免刷屏。
	if (TotalBytes <= 0 && Request.IsValid() && Request->GetResponse().IsValid())
	{
		TotalBytes = int64(Request->GetResponse()->GetContentLength());
	}

	if (TotalBytes > 0)
	{
		const int32 Percent = int32((BytesReceived * 100) / TotalBytes);
		if (LastLoggedPercent < 0 || Percent / 10 != LastLoggedPercent / 10)
		{
			LastLoggedPercent = (Percent / 10) * 10;
			UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate][Download] 进度 %d%%（%lld/%lld 字节）"),
				Percent, BytesReceived, TotalBytes);
		}
	}

	OnProgress.Broadcast(BytesReceived, TotalBytes);
}

void UPakDownloader::HandleCompletion(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	// 防止 ProcessRequest 失败时的同步广播与异步回调重复触发。
	if (bCompleteBroadcast)
	{
		return;
	}
	bCompleteBroadcast = true;

	if (!bWasSuccessful || !Response.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate][Download] 下载失败（连接失败或无响应）：%s"), *CurrentUrl);
		OnComplete.Broadcast(false, CurrentSavePath);
		return;
	}

	if (Response->GetResponseCode() != 200)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate][Download] HTTP 状态码异常：%d（%s）"),
			Response->GetResponseCode(), *CurrentUrl);
		OnComplete.Broadcast(false, CurrentSavePath);
		return;
	}

	const TArray<uint8>& Content = Response->GetContent();
	if (!FFileHelper::SaveArrayToFile(Content, *CurrentSavePath))
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate][Download] 写入文件失败：%s"), *CurrentSavePath);
		OnComplete.Broadcast(false, CurrentSavePath);
		return;
	}

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate][Download] 下载完成：%s（%lld 字节）"),
		*CurrentSavePath, int64(Content.Num()));
	OnComplete.Broadcast(true, CurrentSavePath);
}
