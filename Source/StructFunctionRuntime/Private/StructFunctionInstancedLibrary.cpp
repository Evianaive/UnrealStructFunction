// Copyright 2024-2025 Evianaive. All Rights Reserved.

#include "StructFunctionInstancedLibrary.h"

#include "StructUtils/InstancedStruct.h"
#include "UObject/UnrealType.h"

void UStructFunctionInstancedLibrary::SetStructFunctionMostRecentAddress(const FInstancedStruct& InStruct, UScriptStruct* ExpectedBase, bool bHasBaseStruct)
{
}

DEFINE_FUNCTION(UStructFunctionInstancedLibrary::execSetStructFunctionMostRecentAddress)
{
	P_GET_STRUCT_REF(FInstancedStruct, InStruct)
	P_GET_OBJECT(UScriptStruct, ExpectedBase)
	P_GET_UBOOL(bHasBaseStruct)

	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;

	static FStructProperty StructRefProperty = FStructProperty(EC_InternalUseOnlyConstructor, nullptr);

	const UScriptStruct* ActualStruct = InStruct.GetScriptStruct();
	uint8* ResolvedAddress = const_cast<uint8*>(reinterpret_cast<const uint8*>(InStruct.GetMemory()));
	bool bTypeAccepted = true;
	#if WITH_EDITOR
	if (!bHasBaseStruct)
	{
		ensureMsgf(false, TEXT("StructFunctionInstanced: InstancedStruct property is missing BaseStruct metadata."));
	}
	#endif
	if (bHasBaseStruct && ExpectedBase)
	{
		bTypeAccepted = (ActualStruct != nullptr) && ActualStruct->IsChildOf(ExpectedBase);
		if (!bTypeAccepted)
		{
			ResolvedAddress = nullptr;
			#if WITH_EDITOR
			ensureMsgf(false, TEXT("StructFunctionInstanced: InstancedStruct type '%s' does not match expected base '%s'."), *GetNameSafe(ActualStruct), *GetNameSafe(ExpectedBase));
			#endif
		}
	}

	if (ResolvedAddress && ExpectedBase)
	{
		StructRefProperty.Struct = ExpectedBase;
		Stack.MostRecentProperty = &StructRefProperty;
		Stack.MostRecentPropertyAddress = ResolvedAddress;
	}

	UE_LOG(LogTemp, Display, TEXT("StructFunctionInstanced thunk: expected=%s actual=%s hasBase=%d accepted=%d address=%p"),
		*GetNameSafe(ExpectedBase), *GetNameSafe(ActualStruct), bHasBaseStruct ? 1 : 0, bTypeAccepted ? 1 : 0, ResolvedAddress);

	P_FINISH;
	P_NATIVE_BEGIN;
	P_NATIVE_END;
}
