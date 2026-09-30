// Copyright Epic Games, Inc. All Rights Reserved.

#include "HotUpdateSubsystem.h"

#include "../HotPakDemo.h"

void UHotUpdateSubsystem::NotifyHotfixApplied(const FString& NewVersion)
{
	if (!NewVersion.IsEmpty())
	{
		CurrentVersionString = NewVersion;
	}

	bPatchMounted = true;
	OnHotfixApplied.Broadcast(CurrentVersionString);
}
