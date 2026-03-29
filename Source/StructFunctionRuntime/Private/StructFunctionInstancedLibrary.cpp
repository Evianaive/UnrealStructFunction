// Copyright 2024-2025 Evianaive. All Rights Reserved.

#include "StructFunctionInstancedLibrary.h"

#include "StructUtils/InstancedStruct.h"

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

	const UScriptStruct* ActualStruct = InStruct.GetScriptStruct();
	#if WITH_EDITOR
	if (!bHasBaseStruct)
	{
		ensureMsgf(false, TEXT("StructFunctionInstanced: InstancedStruct property is missing BaseStruct metadata."));
	}
	#endif
	if (ExpectedBase && ActualStruct && ActualStruct->IsChildOf(ExpectedBase))
	{
		Stack.MostRecentPropertyAddress = const_cast<uint8*>(reinterpret_cast<const uint8*>(InStruct.GetMemory()));
	}
	else if (ExpectedBase)
	{
		#if WITH_EDITOR
		ensureMsgf(false, TEXT("StructFunctionInstanced: InstancedStruct type '%s' does not match expected base '%s'."), *GetNameSafe(ActualStruct), *GetNameSafe(ExpectedBase));
		#endif
	}

	P_FINISH;
	P_NATIVE_BEGIN;
	P_NATIVE_END;
}
