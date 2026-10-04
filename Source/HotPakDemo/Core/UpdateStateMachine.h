// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UpdateStateMachine.generated.h"

/**
 * 热更新流程状态（对齐需求文档 4.4）。
 *
 * Day 3 只实现 Idle / Checking / UpToDate / NeedUpdate / ForceUpdate；
 * Downloading / Verifying / Mounting / Done / Failed / RollingBack 为 Day 4~6 占位，
 * 提前定义以便后续下载器、校验器、挂载器共用同一套状态枚举，避免后面改枚举。
 */
UENUM(BlueprintType)
enum class EHotUpdateState : uint8
{
	Idle        UMETA(DisplayName = "Idle"),        // 初始状态
	Checking    UMETA(DisplayName = "Checking"),    // 检测中：拉取远端清单并比对
	UpToDate    UMETA(DisplayName = "UpToDate"),    // 已最新，无需更新
	NeedUpdate  UMETA(DisplayName = "NeedUpdate"),  // 需更新，等待 Day 4 接下载器
	ForceUpdate UMETA(DisplayName = "ForceUpdate"), // 强制更新（本地版本低于最低支持版本）
	Downloading UMETA(DisplayName = "Downloading"), // 下载中（Day 4 占位）
	Verifying   UMETA(DisplayName = "Verifying"),   // 校验中（Day 4 占位）
	Mounting    UMETA(DisplayName = "Mounting"),    // 挂载中（Day 5 占位）
	Done        UMETA(DisplayName = "Done"),        // 完成（Day 5 占位）
	Failed      UMETA(DisplayName = "Failed"),      // 失败（Day 6 占位）
	RollingBack UMETA(DisplayName = "RollingBack")  // 回滚中（Day 6 占位）
};

/** 状态转移广播：携带旧状态与新状态，供日志、UI 与后续流程监听。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHotUpdateStateChanged, EHotUpdateState, OldState, EHotUpdateState, NewState);

/**
 * 更新流程状态机（Day 3 骨架，对齐需求文档 4.4）。
 *
 * 只负责记录当前状态、执行转移并广播 OnStateChanged，不含下载/校验/挂载逻辑。
 * 由 UHotUpdateSubsystem 创建并持有，UVersionManager 在检测完成后调用 TransitionTo()。
 */
UCLASS(BlueprintType)
class HOTPAKDEMO_API UUpdateStateMachine : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 转移到新状态。
	 * 状态未变化时直接忽略；发生变化时打印 LogHotUpdate 日志并广播 OnStateChanged。
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|State")
	void TransitionTo(EHotUpdateState NewState);

	/** 当前状态（默认 Idle）。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|State")
	EHotUpdateState GetCurrentState() const { return CurrentState; }

	/** 返回状态的中文可读名，用于日志与界面显示。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|State")
	static FString GetStateDisplayName(EHotUpdateState State);

	/** 状态变化广播。 */
	UPROPERTY(BlueprintAssignable, Category = "Hot Update|State")
	FOnHotUpdateStateChanged OnStateChanged;

private:
	UPROPERTY(VisibleInstanceOnly, Category = "Hot Update|State")
	EHotUpdateState CurrentState = EHotUpdateState::Idle;
};
