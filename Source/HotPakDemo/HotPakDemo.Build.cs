// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HotPakDemo : ModuleRules
{
	public HotPakDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });

		// PakFile 提供 FPakPlatformFile / FPakFile，用于运行时挂载补丁 Pak（仅 .cpp 使用，故放 Private）。
		PrivateDependencyModuleNames.AddRange(new string[] { "PakFile" });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
