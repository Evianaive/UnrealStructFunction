// Copyright 2024-2025 Evianaive. All Rights Reserved.

#include "K2Node_CallStructFunction.h"

#include "BlueprintActionDatabase.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "SourceCodeNavigation.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace
{
	UScriptStruct* ResolveStructByName(const FString& StructName)
	{
		if (StructName.IsEmpty())
		{
			return nullptr;
		}
		if (UScriptStruct* Struct = UClass::TryFindTypeSlow<UScriptStruct>(StructName))
		{
			return Struct;
		}
		if (UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *StructName))
		{
			return Struct;
		}
		return Cast<UScriptStruct>(LoadObject<UObject>(nullptr, *StructName));
	}

	FString BuildStructFunctionKey(const UFunction* Function)
	{
		if (!Function)
		{
			return FString();
		}
		const FString OwnerName = Function->GetMetaData(TEXT("StructFunctionOwner"));
		const FString OriginalName = Function->GetMetaData(TEXT("StructFunctionOriginalName"));
		FString SignatureKey = Function->GetMetaData(TEXT("StructFunctionSignature"));
		if (OwnerName.IsEmpty() || OriginalName.IsEmpty())
		{
			return Function->GetPathName();
		}
		if (SignatureKey.IsEmpty())
		{
			SignatureKey = Function->HasAnyFunctionFlags(FUNC_Const) ? TEXT("const|") : TEXT("mutable|");
			if (const FProperty* ReturnProp = Function->GetReturnProperty())
			{
				SignatureKey += ReturnProp->GetCPPType(nullptr, CPPF_ArgumentOrReturnValue);
			}
			SignatureKey += TEXT("|");
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
				SignatureKey += Property->GetCPPType(nullptr, CPPF_ArgumentOrReturnValue);
				SignatureKey += TEXT(";");
			}
		}
		return OwnerName + TEXT("|") + OriginalName + TEXT("|") + SignatureKey;
	}

	UScriptStruct* ResolveOwnerStructFromFunction(const UFunction* Function)
	{
		if (!Function)
		{
			return nullptr;
		}

		for (TFieldIterator<FProperty> PropIt(Function); PropIt; ++PropIt)
		{
			const FProperty* Property = *PropIt;
			if (!Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}
			if (!Property->HasMetaData(TEXT("StructFunctionSelf")) && Property->GetName() != TEXT("Target"))
			{
				continue;
			}
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				return StructProperty->Struct;
			}
			break;
		}

		return ResolveStructByName(Function->GetMetaData(TEXT("StructFunctionOwner")));
	}
}

void UK2Node_CallStructFunction::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (!ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		return;
	}

	TSet<FString> RegisteredKeys;
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		for (TFieldIterator<UFunction> FunctionIt(Class, EFieldIteratorFlags::ExcludeSuper); FunctionIt; ++FunctionIt)
		{
			UFunction* Function = *FunctionIt;
			if (!Function->HasMetaData(TEXT("StructFunction")))
			{
				continue;
			}
			if (!Function->GetBoolMetaData(TEXT("BlueprintInternalUseOnly")))
			{
				continue;
			}
			const FString FunctionKey = BuildStructFunctionKey(Function);
			if (RegisteredKeys.Contains(FunctionKey))
			{
				continue;
			}
			RegisteredKeys.Add(FunctionKey);

			UBlueprintFunctionNodeSpawner* Spawner = UBlueprintFunctionNodeSpawner::Create(ActionKey, Function);
			UScriptStruct* OwnerStruct = ResolveStructByName(Function->GetMetaData(TEXT("StructFunctionOwner")));
			bool bRegistered = false;
			if (OwnerStruct)
			{
				ActionRegistrar.AddBlueprintAction(OwnerStruct, Spawner);
				bRegistered = true;
			}
			if (!bRegistered)
			{
				ActionRegistrar.AddBlueprintAction(Spawner);
			}
		}
	}
}

bool UK2Node_CallStructFunction::IsActionFilteredOut(const FBlueprintActionFilter& Filter)
{
	if (Super::IsActionFilteredOut(Filter))
	{
		return true;
	}

	UFunction* Function = GetTargetFunction();
	if (!Function || !Function->HasMetaData(TEXT("StructFunction")))
	{
		return true;
	}

	const FStructProperty* TargetParam = nullptr;
	for (TFieldIterator<FProperty> PropIt(Function); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (Property->HasAnyPropertyFlags(CPF_Parm) && !Property->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			TargetParam = CastField<FStructProperty>(Property);
			break;
		}
	}

	if (!TargetParam || Filter.Context.Pins.Num() == 0)
	{
		return true;
	}

	const UScriptStruct* TargetStruct = TargetParam->Struct;

	for (UEdGraphPin* Pin : Filter.Context.Pins)
	{
		if (!Pin)
		{
			continue;
		}
		if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct)
		{
			continue;
		}
		UScriptStruct* PinStruct = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get());
		if (!PinStruct)
		{
			continue;
		}

		if (PinStruct == TargetStruct)
		{
			return false;
		}
	}

	return true;
}

bool UK2Node_CallStructFunction::CanJumpToDefinition() const
{
	const UFunction* Function = GetTargetFunction();
	if (Function && Function->HasMetaData(TEXT("StructFunction")))
	{
		if (const UScriptStruct* OwnerStruct = ResolveOwnerStructFromFunction(Function))
		{
			if (FSourceCodeNavigation::CanNavigateToStruct(OwnerStruct))
			{
				return true;
			}
		}
	}

	return Super::CanJumpToDefinition();
}

void UK2Node_CallStructFunction::JumpToDefinition() const
{
	const UFunction* Function = GetTargetFunction();
	if (Function && Function->HasMetaData(TEXT("StructFunction")))
	{
		if (const UScriptStruct* OwnerStruct = ResolveOwnerStructFromFunction(Function))
		{
			if (FSourceCodeNavigation::CanNavigateToStruct(OwnerStruct)
				&& FSourceCodeNavigation::NavigateToStruct(OwnerStruct))
			{
				return;
			}
		}
	}

	Super::JumpToDefinition();
}

FText UK2Node_CallStructFunction::GetTooltipText() const
{
	const UFunction* Function = GetTargetFunction();
	if (!Function || !Function->HasMetaData(TEXT("StructFunction")))
	{
		return Super::GetTooltipText();
	}

	FString OriginalName = Function->GetMetaData(TEXT("StructFunctionOriginalName"));
	if (OriginalName.IsEmpty())
	{
		OriginalName = Function->GetName();
	}

	if (const UScriptStruct* OwnerStruct = ResolveOwnerStructFromFunction(Function))
	{
		return FText::Format(NSLOCTEXT("StructFunction", "StructFunctionTooltip", "Struct function {0}\n\nDeclared in {1}"),
			FText::FromString(OriginalName),
			FText::FromString(OwnerStruct->GetName()));
	}

	return FText::Format(NSLOCTEXT("StructFunction", "StructFunctionTooltipNoOwner", "Struct function {0}"),
		FText::FromString(OriginalName));
}

FText UK2Node_CallStructFunction::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	const UFunction* Function = GetTargetFunction();
	if (!Function || !Function->HasMetaData(TEXT("StructFunction")))
	{
		return Super::GetNodeTitle(TitleType);
	}

	FString OriginalName = Function->GetMetaData(TEXT("StructFunctionOriginalName"));
	if (OriginalName.IsEmpty())
	{
		OriginalName = Function->GetName();
	}

	if (TitleType == ENodeTitleType::FullTitle)
	{
		if (const UScriptStruct* OwnerStruct = ResolveOwnerStructFromFunction(Function))
		{
			return FText::Format(NSLOCTEXT("StructFunction", "StructFunctionNodeTitleFull", "{0}\nTarget is {1}"),
				FText::FromString(OriginalName),
				FText::FromString(OwnerStruct->GetName()));
		}
	}

	return FText::FromString(OriginalName);
}
