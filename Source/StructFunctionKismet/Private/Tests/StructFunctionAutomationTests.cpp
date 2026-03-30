// Copyright 2024-2025 Evianaive. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FString BuildSignatureKey(const UFunction* Function)
	{
		FString Signature = Function->HasAnyFunctionFlags(FUNC_Const) ? TEXT("const|") : TEXT("mutable|");
		if (const FProperty* ReturnProp = Function->GetReturnProperty())
		{
			Signature += ReturnProp->GetCPPType(nullptr, CPPF_ArgumentOrReturnValue);
		}
		Signature += TEXT("|");
		for (TFieldIterator<FProperty> PropIt(Function); PropIt; ++PropIt)
		{
			const FProperty* Property = *PropIt;
			if (!Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}
			if (Property->HasMetaData(TEXT("StructFunctionSelf")) || Property->GetName() == TEXT("Target"))
			{
				continue;
			}
			Signature += Property->GetCPPType(nullptr, CPPF_ArgumentOrReturnValue);
			Signature += TEXT(";");
		}
		return Signature;
	}

	FString BuildStructFunctionKey(const UFunction* Function)
	{
		const FString OwnerName = Function->GetMetaData(TEXT("StructFunctionOwner"));
		const FString OriginalName = Function->GetMetaData(TEXT("StructFunctionOriginalName"));
		FString SignatureKey = Function->GetMetaData(TEXT("StructFunctionSignature"));
		if (OwnerName.IsEmpty() || OriginalName.IsEmpty())
		{
			return Function->GetPathName();
		}
		if (SignatureKey.IsEmpty())
		{
			SignatureKey = BuildSignatureKey(Function);
		}
		return OwnerName + TEXT("|") + OriginalName + TEXT("|") + SignatureKey;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStructFunctionDuplicateTest, "StructFunction.Functions.DuplicateCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStructFunctionDuplicateTest::RunTest(const FString& Parameters)
{
	TMap<FString, TArray<const UFunction*>> FunctionsByKey;
	int32 StructFunctionCount = 0;

	for (TObjectIterator<UFunction> It; It; ++It)
	{
		UFunction* Function = *It;
		if (!Function->HasMetaData(TEXT("StructFunction")))
		{
			continue;
		}
		StructFunctionCount++;

		if (!Function->GetBoolMetaData(TEXT("BlueprintInternalUseOnly")))
		{
			AddError(FString::Printf(TEXT("StructFunction missing BlueprintInternalUseOnly: %s"), *Function->GetPathName()));
		}

		const FString Key = BuildStructFunctionKey(Function);
		FunctionsByKey.FindOrAdd(Key).Add(Function);
	}

	if (StructFunctionCount == 0)
	{
		AddError(TEXT("No StructFunction UFunctions found. Expected at least one."));
	}

	for (const TPair<FString, TArray<const UFunction*>>& Pair : FunctionsByKey)
	{
		if (Pair.Value.Num() <= 1)
		{
			continue;
		}
		AddError(FString::Printf(TEXT("Duplicate StructFunction key: %s"), *Pair.Key));
		for (const UFunction* Function : Pair.Value)
		{
			AddError(FString::Printf(TEXT("  %s"), *Function->GetPathName()));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
