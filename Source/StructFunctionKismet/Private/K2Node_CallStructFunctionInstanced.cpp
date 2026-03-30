// Copyright 2024-2025 Evianaive. All Rights Reserved.

#include "K2Node_CallStructFunctionInstanced.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintNodeSpawner.h"
#include "CallFunctionHandler.h"
#include "EdGraphUtilities.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Variable.h"
#include "KismetCompiler.h"
#include "KismetCompilerMisc.h"
#include "Kismet/BlueprintInstancedStructLibrary.h"
#include "SourceCodeNavigation.h"
#include "StructFunctionInstancedLibrary.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

#include "Editor/BlueprintGraph/Private/CallFunctionHandler.cpp"
#include "Editor/BlueprintGraph/Private/PushModelHelpers.cpp"

namespace
{
	const FStructProperty* FindStructFunctionSelfProperty(const UFunction* Function)
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
			return CastField<FStructProperty>(Property);
		}
		return nullptr;
	}

	UScriptStruct* ResolveStructByNameInstanced(const FString& StructName)
	{
		if (StructName.IsEmpty())
		{
			return nullptr;
		}
		if (UScriptStruct* Struct = UClass::TryFindTypeSlow<UScriptStruct>(StructName))
		{
			return Struct;
		}
		if (!StructName.IsEmpty() && StructName[0] == TCHAR('F'))
		{
			const FString StrippedName = StructName.RightChop(1);
			if (UScriptStruct* Struct = UClass::TryFindTypeSlow<UScriptStruct>(StrippedName))
			{
				return Struct;
			}
		}
		if (UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *StructName))
		{
			return Struct;
		}
		return Cast<UScriptStruct>(LoadObject<UObject>(nullptr, *StructName));
	}

	UScriptStruct* ResolveOwnerStructFromFunction(const UFunction* Function)
	{
		if (const FStructProperty* SelfProperty = FindStructFunctionSelfProperty(Function))
		{
			return SelfProperty->Struct;
		}
		return ResolveStructByNameInstanced(Function ? Function->GetMetaData(TEXT("StructFunctionOwner")) : FString());
	}

	UScriptStruct* GetRootStructOwner(UScriptStruct* Struct)
	{
		UScriptStruct* RootStruct = Struct;
		for (UStruct* SuperStruct = Struct ? Struct->GetSuperStruct() : nullptr; SuperStruct; SuperStruct = SuperStruct->GetSuperStruct())
		{
			if (UScriptStruct* SuperScriptStruct = Cast<UScriptStruct>(SuperStruct))
			{
				RootStruct = SuperScriptStruct;
			}
		}
		return RootStruct;
	}

	int32 GetStructInheritanceDepth(const UScriptStruct* Struct)
	{
		int32 Depth = 0;
		for (const UStruct* SuperStruct = Struct ? Struct->GetSuperStruct() : nullptr; SuperStruct; SuperStruct = SuperStruct->GetSuperStruct())
		{
			if (!Cast<UScriptStruct>(SuperStruct))
			{
				break;
			}
			Depth++;
		}
		return Depth;
	}

	UScriptStruct* ResolveBaseStructFromProperty(const FProperty* Property, bool& bHasMeta)
	{
		bHasMeta = false;
		if (!Property)
		{
			return nullptr;
		}
		static const FName BaseStructKey(TEXT("BaseStruct"));
		const FString BaseStructName = Property->GetMetaData(BaseStructKey);
		if (BaseStructName.IsEmpty())
		{
			return nullptr;
		}
		bHasMeta = true;
		if (UScriptStruct* Struct = UClass::TryFindTypeSlow<UScriptStruct>(BaseStructName))
		{
			return Struct;
		}
		return Cast<UScriptStruct>(LoadObject<UObject>(nullptr, *BaseStructName));
	}

	UScriptStruct* ResolveStructTypeFromPin(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return nullptr;
		}
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
		{
			return Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get());
		}
		return nullptr;
	}

	UScriptStruct* ResolveStructTypeFromPinOrNet(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return nullptr;
		}
		if (UScriptStruct* PinStruct = ResolveStructTypeFromPin(Pin))
		{
			return PinStruct;
		}
		if (UEdGraphPin* NetPin = FEdGraphUtilities::GetNetFromPin(const_cast<UEdGraphPin*>(Pin)))
		{
			if (UScriptStruct* NetStruct = ResolveStructTypeFromPin(NetPin))
			{
				return NetStruct;
			}
		}
		for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (UScriptStruct* LinkedStruct = ResolveStructTypeFromPin(LinkedPin))
			{
				return LinkedStruct;
			}
			if (UEdGraphPin* LinkedNetPin = FEdGraphUtilities::GetNetFromPin(const_cast<UEdGraphPin*>(LinkedPin)))
			{
				if (UScriptStruct* LinkedNetStruct = ResolveStructTypeFromPin(LinkedNetPin))
				{
					return LinkedNetStruct;
				}
			}
		}
		return nullptr;
	}

	UScriptStruct* ResolveConcreteTypeFromMakeInstancedNode(const UEdGraphPin* ContextPin)
	{
		const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(ContextPin ? ContextPin->GetOwningNode() : nullptr);
		if (!CallFunctionNode)
		{
			return nullptr;
		}
		const UFunction* TargetFunction = CallFunctionNode->GetTargetFunction();
		if (!TargetFunction
			|| TargetFunction->GetOuterUClass() != UBlueprintInstancedStructLibrary::StaticClass()
			|| TargetFunction->GetFName() != GET_FUNCTION_NAME_CHECKED(UBlueprintInstancedStructLibrary, MakeInstancedStruct))
		{
			return nullptr;
		}
		const UEdGraphPin* ValuePin = CallFunctionNode->FindPin(TEXT("Value"));
		return ResolveStructTypeFromPinOrNet(ValuePin);
	}

	UScriptStruct* ResolveBaseStructFromFunctionPin(const UEdGraphPin* ContextPin, bool& bHasMeta)
	{
		bHasMeta = false;
		const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(ContextPin ? ContextPin->GetOwningNode() : nullptr);
		if (!CallFunctionNode)
		{
			return nullptr;
		}

		const UFunction* TargetFunction = CallFunctionNode->GetTargetFunction();
		if (!TargetFunction)
		{
			return nullptr;
		}

		const FProperty* SourceProperty = nullptr;
		if (ContextPin->Direction == EGPD_Output && ContextPin->PinName == UEdGraphSchema_K2::PN_ReturnValue)
		{
			SourceProperty = TargetFunction->GetReturnProperty();
		}
		if (!SourceProperty)
		{
			SourceProperty = FindFProperty<FProperty>(TargetFunction, ContextPin->PinName);
		}
		if (!SourceProperty)
		{
			return nullptr;
		}

		const FStructProperty* StructProperty = CastField<FStructProperty>(SourceProperty);
		if (!StructProperty || StructProperty->Struct != FInstancedStruct::StaticStruct())
		{
			return nullptr;
		}

		return ResolveBaseStructFromProperty(SourceProperty, bHasMeta);
	}

	UScriptStruct* ResolveContextStructForInstancedPin(const UEdGraphPin* ContextPin, bool& bHasTypeInfo)
	{
		bHasTypeInfo = false;
		if (!ContextPin)
		{
			return nullptr;
		}

		TArray<const UEdGraphPin*> CandidatePins;
		CandidatePins.Add(ContextPin);
		if (UEdGraphPin* NetPin = FEdGraphUtilities::GetNetFromPin(const_cast<UEdGraphPin*>(ContextPin)))
		{
			CandidatePins.AddUnique(NetPin);
		}

		for (const UEdGraphPin* CandidatePin : CandidatePins)
		{
			const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(CandidatePin->GetOwningNode());
			bool bHasMeta = false;
			UScriptStruct* PropertyBaseStruct = ResolveBaseStructFromProperty(VariableNode ? VariableNode->GetPropertyForVariable() : nullptr, bHasMeta);
			if (bHasMeta)
			{
				bHasTypeInfo = true;
				return PropertyBaseStruct;
			}

			if (UScriptStruct* ConcreteStruct = ResolveConcreteTypeFromMakeInstancedNode(CandidatePin))
			{
				bHasTypeInfo = true;
				return ConcreteStruct;
			}

			bool bHasFunctionMeta = false;
			UScriptStruct* FunctionBaseStruct = ResolveBaseStructFromFunctionPin(CandidatePin, bHasFunctionMeta);
			if (bHasFunctionMeta)
			{
				bHasTypeInfo = true;
				return FunctionBaseStruct;
			}
		}

		return nullptr;
	}

	bool IsOwnerCompatibleWithContextStruct(const UScriptStruct* OwnerStruct, const UScriptStruct* ContextStruct)
	{
		return OwnerStruct && ContextStruct
			&& (OwnerStruct->IsChildOf(ContextStruct) || ContextStruct->IsChildOf(OwnerStruct));
	}

	FString BuildStructFunctionKeyForInstanced(const UFunction* Function, const UScriptStruct* OwnerStruct)
	{
		if (!Function)
		{
			return FString();
		}
		const FString OriginalName = Function->GetMetaData(TEXT("StructFunctionOriginalName"));
		FString SignatureKey = Function->GetMetaData(TEXT("StructFunctionSignature"));
		if (!OwnerStruct || OriginalName.IsEmpty())
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
		const UScriptStruct* RootOwner = GetRootStructOwner(const_cast<UScriptStruct*>(OwnerStruct));
		return GetNameSafe(RootOwner) + TEXT("|") + OriginalName + TEXT("|") + SignatureKey;
	}

	FBPTerminal* CreateLiteralObjectTerm(FKismetFunctionContext& Context, UEdGraphNode* SourceNode, UObject* LiteralObject)
	{
		FBPTerminal* Term = Context.CreateLocalTerminal(ETerminalSpecification::TS_Literal);
		Term->bIsLiteral = true;
		Term->Source = SourceNode;
		Term->ObjectLiteral = LiteralObject;
		Term->Type.PinCategory = UEdGraphSchema_K2::PC_Object;
		Term->Type.PinSubCategoryObject = UScriptStruct::StaticClass();
		Term->Name = LiteralObject ? LiteralObject->GetName() : TEXT("None");
		return Term;
	}

	FBPTerminal* CreateLiteralBoolTerm(FKismetFunctionContext& Context, UEdGraphNode* SourceNode, bool bValue)
	{
		FBPTerminal* Term = Context.CreateLocalTerminal(ETerminalSpecification::TS_Literal);
		Term->bIsLiteral = true;
		Term->Source = SourceNode;
		Term->Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		Term->Name = bValue ? TEXT("true") : TEXT("false");
		return Term;
	}
}

const FName UK2Node_CallStructFunctionInstanced::InstancedTargetPinName(TEXT("InstancedTarget"));

void UK2Node_CallStructFunctionInstanced::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	if (UEdGraphPin* TargetPin = GetHiddenTargetPin())
	{
		TargetPin->bHidden = true;
		TargetPin->bNotConnectable = true;
	}

	UEdGraphPin* InstancedPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, FInstancedStruct::StaticStruct(), InstancedTargetPinName);
	InstancedPin->PinFriendlyName = FText::FromString(TEXT("Target"));
}

void UK2Node_CallStructFunctionInstanced::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	const bool bOpenForNodeClass = ActionRegistrar.IsOpenForRegistration(ActionKey);
	const bool bOpenForInstancedStruct = ActionRegistrar.IsOpenForRegistration(FInstancedStruct::StaticStruct());
	UE_LOG(LogTemp, Verbose, TEXT("StructFunctionInstanced GetMenuActions called (class=%d instanced=%d key=%s)"), bOpenForNodeClass ? 1 : 0, bOpenForInstancedStruct ? 1 : 0, *GetNameSafe(ActionRegistrar.GetActionKeyFilter()));
	if (!bOpenForNodeClass && !bOpenForInstancedStruct)
	{
		return;
	}

	struct FInstancedActionCandidate
	{
		UFunction* Function = nullptr;
		UScriptStruct* OwnerStruct = nullptr;
		int32 OwnerDepth = TNumericLimits<int32>::Max();
	};

	TMap<FString, FInstancedActionCandidate> CandidatesByKey;
	int32 RegisteredCount = 0;
	int32 RegisteredForInstanced = 0;
	int32 RegisteredForClass = 0;
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
			if (!Function->HasMetaData(TEXT("StructFunctionInstanced")))
			{
				continue;
			}
			if (!Function->GetBoolMetaData(TEXT("BlueprintInternalUseOnly")))
			{
				continue;
			}
			if (!FBlueprintActionDatabase::IsFunctionAllowed(Function, FBlueprintActionDatabase::EPermissionsContext::Node))
			{
				continue;
			}
			UScriptStruct* OwnerStruct = ResolveOwnerStructFromFunction(Function);
			if (!OwnerStruct)
			{
				UE_LOG(LogTemp, Verbose, TEXT("StructFunctionInstanced GetMenuActions skip (owner unresolved): %s ownerMeta=%s"), *Function->GetPathName(), *Function->GetMetaData(TEXT("StructFunctionOwner")));
				continue;
			}
			const FString FunctionKey = BuildStructFunctionKeyForInstanced(Function, OwnerStruct);
			const int32 OwnerDepth = GetStructInheritanceDepth(OwnerStruct);
			FInstancedActionCandidate* ExistingCandidate = CandidatesByKey.Find(FunctionKey);
			if (ExistingCandidate && ExistingCandidate->OwnerDepth <= OwnerDepth)
			{
				continue;
			}
			FInstancedActionCandidate& Candidate = CandidatesByKey.FindOrAdd(FunctionKey);
			Candidate.Function = Function;
			Candidate.OwnerStruct = OwnerStruct;
			Candidate.OwnerDepth = OwnerDepth;
		}
	}

	for (const TPair<FString, FInstancedActionCandidate>& Pair : CandidatesByKey)
	{
		UFunction* Function = Pair.Value.Function;
		UScriptStruct* OwnerStruct = Pair.Value.OwnerStruct;
		if (!Function || !OwnerStruct)
		{
			continue;
		}
			UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(ActionKey, nullptr,
				UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda([Function](UEdGraphNode* NewNode, bool /*bIsTemplateNode*/)
				{
					UK2Node_CallStructFunctionInstanced* CallNode = CastChecked<UK2Node_CallStructFunctionInstanced>(NewNode);
					CallNode->SetFromFunction(Function);
					CallNode->FunctionReference.SetFromField<UFunction>(Function, false);
				}));
		if (!Spawner)
		{
			UE_LOG(LogTemp, Verbose, TEXT("StructFunctionInstanced failed to create node spawner for %s"), *Function->GetPathName());
			continue;
		}
		Spawner->DefaultMenuSignature.MenuName = FText::FromName(Function->GetFName());
		if (Function->HasMetaData(TEXT("DisplayName")))
		{
			Spawner->DefaultMenuSignature.MenuName = FText::FromString(Function->GetMetaData(TEXT("DisplayName")));
		}
		Spawner->DefaultMenuSignature.Category = Function->GetMetaDataText(TEXT("Category"), TEXT("UObjectCategory"));
		Spawner->DefaultMenuSignature.Tooltip = Function->GetToolTipText();
		if (bOpenForInstancedStruct && ActionRegistrar.AddBlueprintAction(FInstancedStruct::StaticStruct(), Spawner))
		{
			RegisteredForInstanced++;
		}
		else if (bOpenForNodeClass && ActionRegistrar.AddBlueprintAction(ActionKey, Spawner))
		{
			RegisteredForClass++;
		}
		RegisteredCount++;
		UE_LOG(LogTemp, Verbose, TEXT("StructFunctionInstanced register candidate: %s owner=%s open(class=%d instanced=%d)"), *Function->GetPathName(), *GetNameSafe(OwnerStruct), bOpenForNodeClass ? 1 : 0, bOpenForInstancedStruct ? 1 : 0);
	}
	UE_LOG(LogTemp, Verbose, TEXT("StructFunctionInstanced registered candidates=%d accepted(class=%d instanced=%d)"), RegisteredCount, RegisteredForClass, RegisteredForInstanced);
}

bool UK2Node_CallStructFunctionInstanced::IsActionFilteredOut(const FBlueprintActionFilter& Filter)
{
	UFunction* Function = GetTargetFunction();
	if (!Function || !Function->HasMetaData(TEXT("StructFunctionInstanced")))
	{
		return true;
	}

	UScriptStruct* OwnerStruct = ResolveOwnerStructFromFunction(Function);
	if (!OwnerStruct)
	{
		UE_LOG(LogTemp, Verbose, TEXT("StructFunctionInstanced filter out: owner unresolved for %s (ownerMeta=%s)"), *Function->GetPathName(), *Function->GetMetaData(TEXT("StructFunctionOwner")));
		return true;
	}

	for (UEdGraphPin* Pin : Filter.Context.Pins)
	{
		if (!Pin)
		{
			continue;
		}
		if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct
			|| Pin->PinType.PinSubCategoryObject != FInstancedStruct::StaticStruct())
		{
			continue;
		}

		bool bHasTypeInfo = false;
		UScriptStruct* ContextStruct = ResolveContextStructForInstancedPin(Pin, bHasTypeInfo);
		if (!bHasTypeInfo)
		{
			UE_LOG(LogTemp, Verbose, TEXT("StructFunctionInstanced filtered: missing context type info for %s"), *Function->GetPathName());
			return true;
		}
		if (bHasTypeInfo && !ContextStruct)
		{
			return true;
		}
		if (bHasTypeInfo && !IsOwnerCompatibleWithContextStruct(OwnerStruct, ContextStruct))
		{
			UE_LOG(LogTemp, Verbose, TEXT("StructFunctionInstanced filtered by context mismatch: function=%s owner=%s context=%s"), *Function->GetPathName(), *GetNameSafe(OwnerStruct), *GetNameSafe(ContextStruct));
			return true;
		}

		UE_LOG(LogTemp, Verbose, TEXT("StructFunctionInstanced allowed function=%s owner=%s hasTypeInfo=%d"), *Function->GetPathName(), *GetNameSafe(OwnerStruct), bHasTypeInfo ? 1 : 0);
		return false;
	}

	UE_LOG(LogTemp, Verbose, TEXT("StructFunctionInstanced filtered out: no InstancedStruct context pin for %s"), *Function->GetPathName());
	return true;
}

bool UK2Node_CallStructFunctionInstanced::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
{
	if (MyPin == GetInstancedTargetPin())
	{
		if (!OtherPin || OtherPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct
			|| OtherPin->PinType.PinSubCategoryObject != FInstancedStruct::StaticStruct())
		{
			OutReason = TEXT("Target must be an InstancedStruct.");
			return true;
		}

		UFunction* Function = GetTargetFunction();
		UScriptStruct* OwnerStruct = ResolveOwnerStructFromFunction(Function);
		if (OwnerStruct)
		{
			bool bHasTypeInfo = false;
			UScriptStruct* ContextStruct = ResolveContextStructForInstancedPin(OtherPin, bHasTypeInfo);
			if (!bHasTypeInfo)
			{
				OutReason = TEXT("InstancedStruct must provide a BaseStruct or concrete source type.");
				return true;
			}
			if (bHasTypeInfo && !ContextStruct)
			{
				OutReason = TEXT("InstancedStruct context type could not be resolved.");
				return true;
			}
			if (bHasTypeInfo && !IsOwnerCompatibleWithContextStruct(OwnerStruct, ContextStruct))
			{
				OutReason = TEXT("InstancedStruct BaseStruct does not allow this function.");
				return true;
			}
		}
	}

	return Super::IsConnectionDisallowed(MyPin, OtherPin, OutReason);
}

bool UK2Node_CallStructFunctionInstanced::CanJumpToDefinition() const
{
	const UFunction* Function = GetTargetFunction();
	if (Function && Function->HasMetaData(TEXT("StructFunctionInstanced")))
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

void UK2Node_CallStructFunctionInstanced::JumpToDefinition() const
{
	const UFunction* Function = GetTargetFunction();
	if (Function && Function->HasMetaData(TEXT("StructFunctionInstanced")))
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

FText UK2Node_CallStructFunctionInstanced::GetTooltipText() const
{
	const UFunction* Function = GetTargetFunction();
	if (!Function || !Function->HasMetaData(TEXT("StructFunctionInstanced")))
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
		return FText::Format(NSLOCTEXT("StructFunction", "StructFunctionInstancedTooltip", "Instanced struct function {0}\n\nDeclared in {1}"),
			FText::FromString(OriginalName),
			FText::FromString(OwnerStruct->GetName()));
	}

	return FText::Format(NSLOCTEXT("StructFunction", "StructFunctionInstancedTooltipNoOwner", "Instanced struct function {0}"),
		FText::FromString(OriginalName));
}

FText UK2Node_CallStructFunctionInstanced::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	const UFunction* Function = GetTargetFunction();
	if (!Function || !Function->HasMetaData(TEXT("StructFunctionInstanced")))
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
			return FText::Format(NSLOCTEXT("StructFunction", "StructFunctionInstancedNodeTitleFull", "{0}\nTarget is {1}"),
				FText::FromString(OriginalName),
				FText::FromString(OwnerStruct->GetName()));
		}
	}

	return FText::FromString(OriginalName);
}

UEdGraphPin* UK2Node_CallStructFunctionInstanced::GetInstancedTargetPin() const
{
	return FindPin(InstancedTargetPinName);
}

UEdGraphPin* UK2Node_CallStructFunctionInstanced::GetHiddenTargetPin() const
{
	return FindPin(TEXT("Target"));
}

class FKCHandler_CallStructFunctionInstanced : public FKCHandler_CallFunction
{
public:
	FKCHandler_CallStructFunctionInstanced(FKismetCompilerContext& InCompilerContext)
		: FKCHandler_CallFunction(InCompilerContext)
	{
	}

	virtual void Compile(FKismetFunctionContext& Context, UEdGraphNode* Node) override
	{
		UK2Node_CallStructFunctionInstanced* StructNode = CastChecked<UK2Node_CallStructFunctionInstanced>(Node);
		UEdGraphPin* InstancedPin = StructNode->GetInstancedTargetPin();
		const bool bWasOrphaned = InstancedPin ? InstancedPin->bOrphanedPin : false;
		if (InstancedPin)
		{
			InstancedPin->bOrphanedPin = true;
		}

		FKCHandler_CallFunction::Compile(Context, Node);

		if (InstancedPin)
		{
			InstancedPin->bOrphanedPin = bWasOrphaned;
		}

		UFunction* TargetFunction = StructNode->GetTargetFunction();
		UFunction* SetAddressFunction = UStructFunctionInstancedLibrary::StaticClass()
			->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UStructFunctionInstancedLibrary, SetStructFunctionMostRecentAddress));
		if (!TargetFunction || !SetAddressFunction)
		{
			CompilerContext.MessageLog.Error(TEXT("Instanced StructFunction node missing target function."), Node);
			return;
		}

		UEdGraphPin* TargetPin = StructNode->GetHiddenTargetPin();
		if (!TargetPin || !InstancedPin)
		{
			CompilerContext.MessageLog.Error(TEXT("Instanced StructFunction node pins are missing."), Node);
			return;
		}

		UEdGraphPin* InstancedNet = FEdGraphUtilities::GetNetFromPin(InstancedPin);
		FBPTerminal** InstancedTermPtr = Context.NetMap.Find(InstancedNet);
		FBPTerminal** TargetTermPtr = Context.NetMap.Find(TargetPin);
		if (!InstancedTermPtr || !TargetTermPtr)
		{
			CompilerContext.MessageLog.Error(TEXT("Instanced StructFunction node could not resolve pin terms."), Node);
			return;
		}

		UScriptStruct* OwnerStruct = ResolveOwnerStructFromFunction(TargetFunction);
		if (!OwnerStruct)
		{
			CompilerContext.MessageLog.Error(TEXT("Instanced StructFunction node could not resolve owner struct."), Node);
			UE_LOG(LogTemp, Verbose, TEXT("StructFunctionInstanced compile owner unresolved: %s ownerMeta=%s"), *TargetFunction->GetPathName(), *TargetFunction->GetMetaData(TEXT("StructFunctionOwner")));
			return;
		}

		bool bHasTypeInfo = false;
		UScriptStruct* ContextStruct = ResolveContextStructForInstancedPin(InstancedNet ? InstancedNet : InstancedPin, bHasTypeInfo);
		if (!bHasTypeInfo)
		{
			CompilerContext.MessageLog.Error(TEXT("Instanced StructFunction node requires BaseStruct metadata or a concrete MakeInstancedStruct source type."), Node);
		}
		if (bHasTypeInfo && !ContextStruct)
		{
			CompilerContext.MessageLog.Error(TEXT("Instanced StructFunction node context type could not be resolved."), Node);
		}
		if (bHasTypeInfo && ContextStruct && !IsOwnerCompatibleWithContextStruct(OwnerStruct, ContextStruct))
		{
			CompilerContext.MessageLog.Error(TEXT("Instanced StructFunction node is not compatible with context type."), Node);
		}

		FBPTerminal* ExpectedBaseTerm = CreateLiteralObjectTerm(Context, Node, OwnerStruct);
		FBPTerminal* HasBaseTerm = CreateLiteralBoolTerm(Context, Node, bHasTypeInfo);

		FBlueprintCompiledStatement* SetAddressStatement = new FBlueprintCompiledStatement();
		Context.AllGeneratedStatements.Add(SetAddressStatement);
		SetAddressStatement->Type = KCST_CallFunction;
		SetAddressStatement->FunctionToCall = SetAddressFunction;
		SetAddressStatement->RHS.Add(*InstancedTermPtr);
		SetAddressStatement->RHS.Add(ExpectedBaseTerm);
		SetAddressStatement->RHS.Add(HasBaseTerm);

		(*TargetTermPtr)->InlineGeneratedParameter = SetAddressStatement;
	}
};

FNodeHandlingFunctor* UK2Node_CallStructFunctionInstanced::CreateNodeHandler(FKismetCompilerContext& CompilerContext) const
{
	return new FKCHandler_CallStructFunctionInstanced(CompilerContext);
}
