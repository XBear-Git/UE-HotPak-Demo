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

void UVersionManager::Initialize(UUpdateStateMachine* InStateMachine)
{
	StateMachine = InStateMachine;

	// 载入现有记录（保留 StablePakFileName / LastFailedVersion）；本地状态由本管理器持有。
	if (!VersionRecord)
	{
		VersionRecord = UVersionRecord::LoadOrCreateRecord();
	}
}

void UVersionManager::CheckForUpdate()
{
	if (StateMachine)
	{
		StateMachine->TransitionTo(EHotUpdateState::Checking);
	}

	LocalVersion = UVersionRecord::LoadVersion(UVersionRecord::GetBaseVersion());
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
	return UVersionRecord::LoadVersion(UVersionRecord::GetBaseVersion());
}

bool UVersionManager::SaveLocalVersion(const FString& InVersion)
{
	if (!VersionRecord)
	{
		VersionRecord = UVersionRecord::LoadOrCreateRecord();
	}
	return VersionRecord->SaveVersion(InVersion);
}

FString UVersionManager::GetStablePakFileName() const
{
	return VersionRecord ? VersionRecord->StablePakFileName : FString();
}

FString UVersionManager::GetLastFailedVersion() const
{
	return VersionRecord ? VersionRecord->LastFailedVersion : FString();
}

bool UVersionManager::SaveStableInfo(const FString& InVersion, const FString& InPakFileName)
{
	if (!VersionRecord)
	{
		VersionRecord = UVersionRecord::LoadOrCreateRecord();
	}
	return VersionRecord->SaveStableInfo(InVersion, InPakFileName);
}

bool UVersionManager::SaveLastFailedVersion(const FString& InVersion)
{
	if (!VersionRecord)
	{
		VersionRecord = UVersionRecord::LoadOrCreateRecord();
	}
	return VersionRecord->SaveLastFailedVersion(InVersion);
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

	// Day 4：解析 pak 下载信息（url / size / sha256 / order / mount_point）。
	PakDownloadInfo = FPakDownloadInfo();
	const TSharedPtr<FJsonObject>* PakObject = nullptr;
	if (Root->TryGetObjectField(TEXT("pak"), PakObject) && PakObject && PakObject->IsValid())
	{
		const TSharedPtr<FJsonObject>& Pak = *PakObject;
		Pak->TryGetStringField(TEXT("url"), PakDownloadInfo.Url);
		Pak->TryGetStringField(TEXT("sha256"), PakDownloadInfo.Sha256);
		Pak->TryGetStringField(TEXT("mount_point"), PakDownloadInfo.MountPoint);
		Pak->TryGetNumberField(TEXT("size"), PakDownloadInfo.Size);
		Pak->TryGetNumberField(TEXT("order"), PakDownloadInfo.Order);

		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 解析到 Pak 下载信息：url=%s，size=%lld，sha256=%s，order=%d。"),
			*PakDownloadInfo.Url, PakDownloadInfo.Size, *PakDownloadInfo.Sha256, PakDownloadInfo.Order);
	}
	else
	{
		UE_LOG(LogHotUpdate, Warning, TEXT("[HotUpdate] version.json 缺少 pak 字段，无法下载增量包。"));
	}

	RemoteVersion = LatestVersion;
	LocalVersion = UVersionRecord::LoadVersion(UVersionRecord::GetBaseVersion());

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

	// 注意：版本写入的唯一入口是「挂载成功后」（UHotUpdateSubsystem::MountDownloadedPak
	// 调用 SaveLocalVersion）。检测阶段不写盘，避免 local > remote 时把本地版本降级。

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
