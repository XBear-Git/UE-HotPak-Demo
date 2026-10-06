// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 自带的可移植 SHA-256 实现（Day 4 / FR-06）。
 *
 * 背景：UE 5.7 的 FPlatformMisc::GetSHA256Signature 在 Windows 平台没有实现——
 * GenericPlatformMisc.cpp 里该函数体直接 `checkf(false, "No SHA256 Platform implementation")`，
 * Windows 也没有 override，因此不能用于校验下载文件。
 * 这里内置一份标准 SHA-256，输出与 Python 端 `hashlib.sha256().hexdigest()`
 * 完全一致（64 位小写十六进制），无需改动补丁脚本。
 */
namespace HotUpdateSha256
{
	/** 计算字节数组的 SHA-256，返回 64 位小写十六进制字符串。 */
	FString HashBytes(const uint8* Data, int64 Size);

	/** 分块读取文件计算 SHA-256；成功返回 true 并输出 64 位小写十六进制。 */
	bool HashFile(const FString& FilePath, FString& OutHex);

	/** 对若干已知向量做自测，全部通过返回 true，并把每个向量结果打印到 LogHotUpdate。 */
	bool RunSelfTest();
}
