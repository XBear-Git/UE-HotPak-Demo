// Copyright Epic Games, Inc. All Rights Reserved.

#include "VersionManager.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "VersionRecord.h"

#include "../HotPakDemo.h"

namespace
{
	// 本地没有存档时的默认基线版本，与基础包一致。
	const TCHAR* DefaultLocalVersion = TEXT("1.0.0");
}

void UVersionManager::Initialize(UUpdateStateMachine* InStateMachine)
{
	StateMachine = InStateMachine;

	// 持久化对象由本管理器持有（对应 FR-08），生命周期与 GameInstance 对齐。
	if (!VersionRecord)
	{
		VersionRecord = NewObject<UVersionRecord>(this);
	}
}

void UVersionManager::CheckForUpdate()
{
	if (StateMachine)
	{
		StateMachine->TransitionTo(EHotUpdateState::Checking);
	}

	LocalVersion = UVersionRecord::LoadVersion(DefaultLocalVersion);
	RemoteVersion.Reset();

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 开始检测更新：本地版本=%s，远端清单=%s。"),
		*LocalVersion, *VersionManifestUrl);

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(VersionManifestUrl);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	// BindUObject 使用弱引用，UVersionManager 销毁后回调不会悬空（见文档 6.2）。
	Request->OnProcessRequestComplete().BindUObject(this, &UVersionManager::HandleVersionResponse);

	if (!Request->ProcessRequest())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] 无法发起版本清单请求：%s。"), *VersionManifestUrl);
		FinishCheck(EHotUpdateState::Failed);
	}
}

FString UVersionManager::QueryLocalVersion() const
{
	// 直接从存档读取，避免在尚未检测或检测失败时拿到空的缓存值。
	return UVersionRecord::LoadVersion(DefaultLocalVersion);
}

int32 UVersionManager::CompareVersion(const FString& VersionA, const FString& VersionB)
{
	TArray<FString> PartsA;
	TArray<FString> PartsB;
	VersionA.ParseIntoArray(PartsA, TEXT("."), /*InCullEmpty=*/true);
	VersionB.ParseIntoArray(PartsB, TEXT("."), /*InCullEmpty=*/true);

	const int32 SegmentCount = FMath::Max(PartsA.Num(), PartsB.Num());
	for (int32 Index = 0; Index < SegmentCount; ++Index)
	{
		const int32 ValueA = PartsA.IsValidIndex(Index) ? FCString::Atoi(*PartsA[Index]) : 0;
		const int32 ValueB = PartsB.IsValidIndex(Index) ? FCString::Atoi(*PartsB[Index]) : 0;

		if (ValueA < ValueB)
		{
			return -1;
		}
		if (ValueA > ValueB)
		{
			return 1;
		}
	}

	return 0;
}

void UVersionManager::HandleVersionResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	// 异步回调可能晚于对象销毁，这里只通过成员安全访问；BindUObject 已保证不悬空。
	if (!bWasSuccessful || !Response.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] 版本清单请求失败（连接失败或无响应）。"));
		FinishCheck(EHotUpdateState::Failed);
		return;
	}

	if (Response->GetResponseCode() != 200)
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] 版本清单返回异常状态码：%d。"), Response->GetResponseCode());
		FinishCheck(EHotUpdateState::Failed);
		return;
	}

	ProcessManifest(Response->GetContentAsString());
}

void UVersionManager::ProcessManifest(const FString& JsonText)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] 版本清单 JSON 解析失败。"));
		FinishCheck(EHotUpdateState::Failed);
		return;
	}

	FString LatestVersion;
	if (!Root->TryGetStringField(TEXT("latest_version"), LatestVersion) || LatestVersion.IsEmpty())
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate] 版本清单缺少 latest_version 字段。"));
		FinishCheck(EHotUpdateState::Failed);
		return;
	}

	FString MinSupportedClient;
	Root->TryGetStringField(TEXT("min_supported_client"), MinSupportedClient);

	bool bForceUpdate = false;
	Root->TryGetBoolField(TEXT("force_update"), bForceUpdate);

	RemoteVersion = LatestVersion;
	LocalVersion = UVersionRecord::LoadVersion(DefaultLocalVersion);

	const int32 CompareToLatest = CompareVersion(LocalVersion, RemoteVersion);
	const bool bBelowMinSupported = !MinSupportedClient.IsEmpty()
		&& CompareVersion(LocalVersion, MinSupportedClient) < 0;

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 版本比对：本地=%s，远端=%s，最低支持=%s，force_update=%s。"),
		*LocalVersion,
		*RemoteVersion,
		MinSupportedClient.IsEmpty() ? TEXT("<未设置>") : *MinSupportedClient,
		bForceUpdate ? TEXT("true") : TEXT("false"));

	// 判定优先级：低于最低支持版本 > 远端强制更新 > 普通可更新 > 已最新。
	EHotUpdateState ResultState = EHotUpdateState::UpToDate;
	if (bBelowMinSupported || (bForceUpdate && CompareToLatest < 0))
	{
		ResultState = EHotUpdateState::ForceUpdate;
		UE_LOG(LogHotUpdate, Warning, TEXT("[HotUpdate] 强制更新：本地 %s 已低于最低支持版本 %s。"),
			*LocalVersion, MinSupportedClient.IsEmpty() ? *RemoteVersion : *MinSupportedClient);
	}
	else if (CompareToLatest < 0)
	{
		ResultState = EHotUpdateState::NeedUpdate;
		UE_LOG(LogHotUpdate, Warning, TEXT("[HotUpdate] 需更新：%s -> %s（Day 4 接入下载器）。"),
			*LocalVersion, *RemoteVersion);
	}
	else
	{
		ResultState = EHotUpdateState::UpToDate;
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 已最新：本地 %s 不早于远端 %s。"),
			*LocalVersion, *RemoteVersion);
	}

	// Day 3 验证用：判定为「已最新」时把远端版本写入本地存档，
	// 便于下一轮启动直接命中「已最新」分支。真正的挂载后写入在 Day 5。
	if (ResultState == EHotUpdateState::UpToDate && VersionRecord)
	{
		VersionRecord->SaveVersion(RemoteVersion);
	}

	FinishCheck(ResultState);
}

void UVersionManager::FinishCheck(EHotUpdateState ResultState)
{
	if (StateMachine)
	{
		StateMachine->TransitionTo(ResultState);
	}

	OnVersionCheckCompleted.Broadcast(ResultState, RemoteVersion);
}
