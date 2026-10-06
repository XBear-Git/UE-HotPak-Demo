// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "PakDownloader.generated.h"

/** 下载进度广播：已接收字节 / 总字节（总字节未知时为 0）。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPakDownloadProgress, int64, BytesReceived, int64, TotalBytes);

/** 下载完成广播：成功与否 + 本地文件路径。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPakDownloadComplete, bool, bSuccess, const FString&, LocalPath);

/**
 * 单文件串行 Pak 下载器（Day 4 / FR-05）。
 *
 * 基于 FHttpModule 异步下载；调用方给出目标路径（通常是 `xxx.pak.tmp`），
 * 下载完成后把响应体一次性写入该路径并广播 OnComplete。
 * 不做断点续传与并发（符合决策边界）。
 */
UCLASS(BlueprintType)
class HOTPAKDEMO_API UPakDownloader : public UObject
{
	GENERATED_BODY()

public:
	/** 开始下载。InSavePath 为目标文件路径（含 `.tmp` 后缀由调用方决定）。 */
	void StartDownload(const FString& InUrl, const FString& InSavePath);

	/** 已接收字节数（供 UI 查询）。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|Download")
	int64 GetBytesReceived() const { return BytesReceived; }

	/** 总字节数；未知时为 0。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|Download")
	int64 GetTotalBytes() const { return TotalBytes; }

	/** 下载进度广播。 */
	UPROPERTY(BlueprintAssignable, Category = "Hot Update|Download")
	FOnPakDownloadProgress OnProgress;

	/** 下载完成广播。 */
	UPROPERTY(BlueprintAssignable, Category = "Hot Update|Download")
	FOnPakDownloadComplete OnComplete;

private:
	/** 进度回调：累计字节并按 10% 粒度打日志。 */
	void HandleProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived64);

	/** 完成回调：校验 HTTP 状态码后把响应体写入目标文件。 */
	void HandleCompletion(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	FString CurrentUrl;
	FString CurrentSavePath;
	int64 BytesReceived = 0;
	int64 TotalBytes = 0;
	int32 LastLoggedPercent = -1;
	bool bCompleteBroadcast = false;
};
