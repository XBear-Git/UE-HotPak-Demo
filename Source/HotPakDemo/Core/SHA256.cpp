// Copyright Epic Games, Inc. All Rights Reserved.

#include "SHA256.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"

#include "../HotPakDemo.h"

namespace
{
	FORCEINLINE uint32 RotateRight(uint32 Value, uint32 Bits)
	{
		return (Value >> Bits) | (Value << (32 - Bits));
	}

	// SHA-256 轮常量。
	const uint32 K[64] = {
		0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
		0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
		0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
		0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
		0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
		0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
		0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
		0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
	};

	/** 单文件 SHA-256 计算上下文（Init / Update / Finalize）。 */
	struct FSha256Context
	{
		uint32 State[8];
		uint64 BitLength;
		uint8 Buffer[64];
		uint32 BufferSize;

		void Reset()
		{
			State[0] = 0x6a09e667u; State[1] = 0xbb67ae85u; State[2] = 0x3c6ef372u; State[3] = 0xa54ff53au;
			State[4] = 0x510e527fu; State[5] = 0x9b05688cu; State[6] = 0x1f83d9abu; State[7] = 0x5be0cd19u;
			BitLength = 0;
			BufferSize = 0;
			FMemory::Memzero(Buffer, sizeof(Buffer));
		}

		void ProcessBlock(const uint8* Block)
		{
			uint32 W[64];
			for (int32 Index = 0; Index < 16; ++Index)
			{
				W[Index] = (uint32(Block[Index * 4]) << 24)
					| (uint32(Block[Index * 4 + 1]) << 16)
					| (uint32(Block[Index * 4 + 2]) << 8)
					| uint32(Block[Index * 4 + 3]);
			}
			for (int32 Index = 16; Index < 64; ++Index)
			{
				const uint32 S0 = RotateRight(W[Index - 15], 7) ^ RotateRight(W[Index - 15], 18) ^ (W[Index - 15] >> 3);
				const uint32 S1 = RotateRight(W[Index - 2], 17) ^ RotateRight(W[Index - 2], 19) ^ (W[Index - 2] >> 10);
				W[Index] = W[Index - 16] + S0 + W[Index - 7] + S1;
			}

			uint32 A = State[0], B = State[1], C = State[2], D = State[3];
			uint32 E = State[4], F = State[5], G = State[6], H = State[7];

			for (int32 Index = 0; Index < 64; ++Index)
			{
				const uint32 S1 = RotateRight(E, 6) ^ RotateRight(E, 11) ^ RotateRight(E, 25);
				const uint32 Ch = (E & F) ^ ((~E) & G);
				const uint32 Temp1 = H + S1 + Ch + K[Index] + W[Index];
				const uint32 S0 = RotateRight(A, 2) ^ RotateRight(A, 13) ^ RotateRight(A, 22);
				const uint32 Maj = (A & B) ^ (A & C) ^ (B & C);
				const uint32 Temp2 = S0 + Maj;

				H = G; G = F; F = E; E = D + Temp1;
				D = C; C = B; B = A; A = Temp1 + Temp2;
			}

			State[0] += A; State[1] += B; State[2] += C; State[3] += D;
			State[4] += E; State[5] += F; State[6] += G; State[7] += H;
		}

		void Update(const uint8* Data, int64 Size)
		{
			for (int64 Index = 0; Index < Size; ++Index)
			{
				Buffer[BufferSize++] = Data[Index];
				if (BufferSize == 64)
				{
					ProcessBlock(Buffer);
					BitLength += 512;
					BufferSize = 0;
				}
			}
		}

		void Finalize(uint8 OutDigest[32])
		{
			const uint64 TotalBits = BitLength + uint64(BufferSize) * 8;

			Buffer[BufferSize++] = 0x80;
			if (BufferSize > 56)
			{
				while (BufferSize < 64)
				{
					Buffer[BufferSize++] = 0;
				}
				ProcessBlock(Buffer);
				BufferSize = 0;
			}
			while (BufferSize < 56)
			{
				Buffer[BufferSize++] = 0;
			}
			for (int32 Index = 0; Index < 8; ++Index)
			{
				Buffer[56 + Index] = uint8((TotalBits >> (56 - Index * 8)) & 0xFF);
			}
			ProcessBlock(Buffer);

			for (int32 Index = 0; Index < 8; ++Index)
			{
				OutDigest[Index * 4]     = uint8((State[Index] >> 24) & 0xFF);
				OutDigest[Index * 4 + 1] = uint8((State[Index] >> 16) & 0xFF);
				OutDigest[Index * 4 + 2] = uint8((State[Index] >> 8) & 0xFF);
				OutDigest[Index * 4 + 3] = uint8(State[Index] & 0xFF);
			}
		}
	};

	FString DigestToHex(const uint8* Digest)
	{
		static const TCHAR* HexDigits = TEXT("0123456789abcdef");
		FString Result;
		Result.Reserve(64);
		for (int32 Index = 0; Index < 32; ++Index)
		{
			Result.AppendChar(HexDigits[(Digest[Index] >> 4) & 0xF]);
			Result.AppendChar(HexDigits[Digest[Index] & 0xF]);
		}
		return Result;
	}
}

FString HotUpdateSha256::HashBytes(const uint8* Data, int64 Size)
{
	FSha256Context Context;
	Context.Reset();
	Context.Update(Data, Size);

	uint8 Digest[32];
	Context.Finalize(Digest);
	return DigestToHex(Digest);
}

bool HotUpdateSha256::HashFile(const FString& FilePath, FString& OutHex)
{
	OutHex.Reset();

	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*FilePath));
	if (!Reader)
	{
		return false;
	}

	FSha256Context Context;
	Context.Reset();

	constexpr int64 ChunkSize = 64 * 1024;
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(int32(ChunkSize));

	int64 Remaining = Reader->TotalSize();
	while (Remaining > 0)
	{
		const int64 BytesToRead = FMath::Min(Remaining, ChunkSize);
		Reader->Serialize(Buffer.GetData(), BytesToRead);
		if (Reader->IsError())
		{
			return false;
		}
		Context.Update(Buffer.GetData(), BytesToRead);
		Remaining -= BytesToRead;
	}

	uint8 Digest[32];
	Context.Finalize(Digest);
	OutHex = DigestToHex(Digest);
	return true;
}

bool HotUpdateSha256::RunSelfTest()
{
	struct FCase
	{
		const TCHAR* Input;
		const TCHAR* Expected;
	};

	static const FCase Cases[] = {
		{ TEXT(""), TEXT("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") },
		{ TEXT("abc"), TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") },
		{ TEXT("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), TEXT("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") },
		{ TEXT("The quick brown fox jumps over the lazy dog"), TEXT("d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592") },
	};

	bool bAllPassed = true;
	for (const FCase& Case : Cases)
	{
		const FTCHARToUTF8 Utf8(Case.Input);
		const FString Actual = HashBytes(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		const bool bPassed = Actual.Equals(Case.Expected, ESearchCase::IgnoreCase);
		bAllPassed &= bPassed;

		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate][SHA256] 自测%s：输入=\"%s\"，结果=%s"),
			bPassed ? TEXT("通过") : TEXT("失败"), Case.Input, *Actual);
		if (!bPassed)
		{
			UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate][SHA256] 期望值=%s"), Case.Expected);
		}
	}

	if (bAllPassed)
	{
		UE_LOG(LogHotUpdate, Log, TEXT("[HotUpdate][SHA256] 已知向量自测全部通过。"));
	}
	else
	{
		UE_LOG(LogHotUpdate, Error, TEXT("[HotUpdate][SHA256] 已知向量自测存在失败，下载校验将不可靠！"));
	}
	return bAllPassed;
}
