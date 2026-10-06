// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PakVerifier.generated.h"

/** 校验结果（便于日志区分失败原因）。 */
UENUM(BlueprintType)
enum class EPakVerifyResult : uint8
{
	Success     UMETA(DisplayName = "Success"),      // 通过
	FileNotFound UMETA(DisplayName = "FileNotFound"), // 文件不存在
	SizeMismatch UMETA(DisplayName = "SizeMismatch"), // 大小不符
	HashMismatch UMETA(DisplayName = "HashMismatch"), // SHA-256 不符
	ReadError    UMETA(DisplayName = "ReadError")     // 读取失败
};

/**
 * 下载文件完整性校验器（Day 4 / FR-06）。
 *
 * 先比大小（快），再算 SHA-256（慢）；哈希用自带可移植实现（见 SHA256.h）。
 */
UCLASS()
class HOTPAKDEMO_API UPakVerifier : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 校验文件。
	 * @param FilePath        待校验文件（通常是 `.pak.tmp`）。
	 * @param ExpectedSha256  期望 SHA-256（大小写不敏感）；为空则跳过哈希比对。
	 * @param ExpectedSize    期望字节数；<= 0 则跳过大小比对。
	 */
	UFUNCTION(BlueprintCallable, Category = "Hot Update|Verify")
	EPakVerifyResult Verify(const FString& FilePath, const FString& ExpectedSha256, int64 ExpectedSize);

	/** 最近一次实际算出的 SHA-256（失败时也保留，便于日志排查）。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|Verify")
	FString GetLastActualHash() const { return LastActualHash; }

	/** 结果的中文可读名。 */
	UFUNCTION(BlueprintPure, Category = "Hot Update|Verify")
	static FString GetResultDisplayName(EPakVerifyResult Result);

private:
	FString LastActualHash;
};
