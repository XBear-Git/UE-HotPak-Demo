// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "UpdateStateMachine.h"
#include "VersionManager.generated.h"

class UVersionRecord;

/** 一次版本检测完成后的广播：结果状态 + 远端版本号。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVersionCheckCompleted, EHotUpdateState, ResultState, const FString&, RemoteVersion);

/**
 * 版本清单拉取与比对（对应 FR-04）。
 *
 * 用 FHttpModule 异步 GET 远端 version.json，解析 latest_version /
 * min_supported_client（force_update 可选），再与本地持久化版本比对，
 * 最后驱动 UUpdateStateMachine 转移到 UpToDate / NeedUpdate / ForceUpdate。
 *
 * Day 3 只做到「检测出需更新」，不负责下载与挂载（Day 4/5）。
 */
UCLASS(BlueprintType)
class HOTPAKDEMO_API UVersionManager : public UObject
{
	GENERATED_BODY()

public:
	/** 注入外部持有的状态机；由 UHotUpdateSubsystem 在创建后调用一次。 */
	void Initialize(UUpdateStateMachine* InStateMachine);

	/** 发起异步检测：拉取远端清单并与本地版本比对，驱动状态机转移。 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|Version")
	void CheckForUpdate();

	/**
	 * 语义化版本比较：返回 -1(a<b) / 0(a==b) / 1(a>b)。
	 * 按 "." 分段做数字比较，避免 "1.10.0" 被当成小于 "1.9.0" 的字符串错误。
	 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|Version")
	static int32 CompareVersion(const FString& VersionA, const FString& VersionB);

	/** 最近一次检测时读取到的本地版本（检测前为空）。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|Version")
	FString GetLocalVersion() const { return LocalVersion; }

	/** 即时从本地存档读取版本（不依赖上一次检测缓存），供状态查询使用。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|Version")
	FString QueryLocalVersion() const;

	/** 最近一次检测到的远端版本。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|Version")
	FString GetRemoteVersion() const { return RemoteVersion; }

	/** 远端版本清单地址，可配置以便换端口测试。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hot Update|Version")
	FString VersionManifestUrl = TEXT("http://127.0.0.1:8000/version.json");

	/** 检测完成广播。 */
	UPROPERTY(BlueprintAssignable, Category = "Hot Update|Version")
	FOnVersionCheckCompleted OnVersionCheckCompleted;

private:
	/** HTTP 回调：校验响应状态后交给 ProcessManifest 解析。 */
	void HandleVersionResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	/** 解析 version.json，执行版本比对并驱动状态机。 */
	void ProcessManifest(const FString& JsonText);

	/** 统一收尾：转移状态机 + 广播 OnVersionCheckCompleted（失败时 RemoteVersion 为空）。 */
	void FinishCheck(EHotUpdateState ResultState);

	/** 状态机引用（由 UHotUpdateSubsystem 持有并保证生命周期）。 */
	UPROPERTY(Transient)
	TObjectPtr<UUpdateStateMachine> StateMachine;

	/** 本地版本持久化对象。 */
	UPROPERTY(Transient)
	TObjectPtr<UVersionRecord> VersionRecord;

	FString LocalVersion;
	FString RemoteVersion;
};
