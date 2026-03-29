// Copyright 2024-2025 Evianaive. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "StructUtils/InstancedStruct.h"
#include "StructFunctionInstancedLibrary.generated.h"

class UScriptStruct;

UCLASS()
class STRUCTFUNCTIONRUNTIME_API UStructFunctionInstancedLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(CustomThunk, BlueprintInternalUseOnly)
	static void SetStructFunctionMostRecentAddress(const FInstancedStruct& InStruct, UScriptStruct* ExpectedBase, bool bHasBaseStruct);
	DECLARE_FUNCTION(execSetStructFunctionMostRecentAddress);
};
