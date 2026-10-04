// Copyright Epic Games, Inc. All Rights Reserved.

#include "UpdateStateMachine.h"

#include "../HotPakDemo.h"

void UUpdateStateMachine::TransitionTo(EHotUpdateState NewState)
{
	// 状态未变化不产生转移，避免重复广播（例如连续两次检测都被判定为 UpToDate）。
	if (CurrentState == NewState)
	{
		return;
	}

	const EHotUpdateState OldState = CurrentState;
	CurrentState = NewState;

	UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate] 状态转移：%s -> %s"),
		*GetStateDisplayName(OldState), *GetStateDisplayName(NewState));

	OnStateChanged.Broadcast(OldState, NewState);
}

FString UUpdateStateMachine::GetStateDisplayName(EHotUpdateState State)
{
	switch (State)
	{
	case EHotUpdateState::Idle:        return TEXT("Idle(初始)");
	case EHotUpdateState::Checking:    return TEXT("Checking(检测中)");
	case EHotUpdateState::UpToDate:    return TEXT("UpToDate(已最新)");
	case EHotUpdateState::NeedUpdate:  return TEXT("NeedUpdate(需更新)");
	case EHotUpdateState::ForceUpdate: return TEXT("ForceUpdate(强制更新)");
	case EHotUpdateState::Downloading: return TEXT("Downloading(下载中)");
	case EHotUpdateState::Verifying:   return TEXT("Verifying(校验中)");
	case EHotUpdateState::Mounting:    return TEXT("Mounting(挂载中)");
	case EHotUpdateState::Done:        return TEXT("Done(完成)");
	case EHotUpdateState::Failed:      return TEXT("Failed(失败)");
	case EHotUpdateState::RollingBack: return TEXT("RollingBack(回滚中)");
	default:                           return TEXT("Unknown(未知)");
	}
}
