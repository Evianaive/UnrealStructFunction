// Copyright 2024-2026 Evianaive. All Rights Reserved.

#include "StructFunctionInstancedBaseStructCustomization.h"

#include "BlueprintEditor.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallStructFunctionInstanced.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Tunnel.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet/BlueprintInstancedStructLibrary.h"
#include "Modules/ModuleManager.h"
#include "StructUtils/InstancedStruct.h"
#include "StructViewerFilter.h"
#include "StructViewerModule.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "StructFunctionInstancedBaseStructCustomization"

namespace
{
	constexpr TCHAR BaseStructMetaKey[] = TEXT("BaseStruct");
	constexpr TCHAR CustomizationFunctionPinBaseStructMetaPrefix[] = TEXT("StructFunctionBaseStruct_");
	constexpr TCHAR CustomizationFunctionPinBaseStructModePrefix[] = TEXT("StructFunctionBaseStructMode_");

	enum class EFunctionPinBaseStructMode : uint8
	{
		None,
		Auto,
		Manual,
	};

	struct FInferredConstraint
	{
		UScriptStruct* Struct = nullptr;
		bool bHasAny = false;
		bool bConflict = false;
	};

	FName BuildCustomizationFunctionPinBaseStructMetaKey(const FName& PinName)
	{
		return FName(*(FString(CustomizationFunctionPinBaseStructMetaPrefix) + PinName.ToString()));
	}

	FName BuildCustomizationFunctionPinModeMetaKey(const FName& PinName)
	{
		return FName(*(FString(CustomizationFunctionPinBaseStructModePrefix) + PinName.ToString()));
	}

	const FKismetUserDeclaredFunctionMetadata* GetTerminatorMetadata(const UK2Node_EditablePinBase* EntryNode)
	{
		if (const UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(EntryNode))
		{
			return &FunctionEntry->MetaData;
		}
		if (const UK2Node_Tunnel* TunnelEntry = Cast<UK2Node_Tunnel>(EntryNode))
		{
			return TunnelEntry->DrawNodeAsEntry() ? &TunnelEntry->MetaData : nullptr;
		}
		return nullptr;
	}

	FKismetUserDeclaredFunctionMetadata* GetMutableTerminatorMetadata(UK2Node_EditablePinBase* EntryNode)
	{
		if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(EntryNode))
		{
			return &FunctionEntry->MetaData;
		}
		if (UK2Node_Tunnel* TunnelEntry = Cast<UK2Node_Tunnel>(EntryNode))
		{
			return TunnelEntry->DrawNodeAsEntry() ? &TunnelEntry->MetaData : nullptr;
		}
		return nullptr;
	}

	UScriptStruct* ResolveStructFromPath(const FString& StructPath)
	{
		if (StructPath.IsEmpty())
		{
			return nullptr;
		}
		if (UScriptStruct* StructType = UClass::TryFindTypeSlow<UScriptStruct>(StructPath))
		{
			return StructType;
		}
		return Cast<UScriptStruct>(LoadObject<UObject>(nullptr, *StructPath));
	}

	UScriptStruct* GetConfiguredPinBaseStruct(const UK2Node_EditablePinBase* EntryNode, const FName& PinName)
	{
		const FKismetUserDeclaredFunctionMetadata* Metadata = GetTerminatorMetadata(EntryNode);
		if (!Metadata)
		{
			return nullptr;
		}

		const FName MetaKey = BuildCustomizationFunctionPinBaseStructMetaKey(PinName);
		if (!Metadata->HasMetaData(MetaKey))
		{
			return nullptr;
		}

		return ResolveStructFromPath(Metadata->GetMetaData(MetaKey));
	}

	EFunctionPinBaseStructMode GetConfiguredPinMode(const UK2Node_EditablePinBase* EntryNode, const FName& PinName)
	{
		const FKismetUserDeclaredFunctionMetadata* Metadata = GetTerminatorMetadata(EntryNode);
		if (!Metadata)
		{
			return EFunctionPinBaseStructMode::None;
		}

		const FName ModeKey = BuildCustomizationFunctionPinModeMetaKey(PinName);
		if (!Metadata->HasMetaData(ModeKey))
		{
			return Metadata->HasMetaData(BuildCustomizationFunctionPinBaseStructMetaKey(PinName))
				? EFunctionPinBaseStructMode::Manual
				: EFunctionPinBaseStructMode::None;
		}

		const FString Value = Metadata->GetMetaData(ModeKey);
		if (Value == TEXT("Auto"))
		{
			return EFunctionPinBaseStructMode::Auto;
		}
		if (Value == TEXT("Manual"))
		{
			return EFunctionPinBaseStructMode::Manual;
		}
		return EFunctionPinBaseStructMode::None;
	}

	void SetConfiguredPinBaseStruct(UK2Node_EditablePinBase* EntryNode, UBlueprint* Blueprint, const FName& PinName, const UScriptStruct* InStruct, EFunctionPinBaseStructMode Mode)
	{
		if (!EntryNode || !Blueprint)
		{
			return;
		}

		FKismetUserDeclaredFunctionMetadata* Metadata = GetMutableTerminatorMetadata(EntryNode);
		if (!Metadata)
		{
			return;
		}

		EntryNode->Modify();
		const FName MetaKey = BuildCustomizationFunctionPinBaseStructMetaKey(PinName);
		const FName ModeKey = BuildCustomizationFunctionPinModeMetaKey(PinName);

		if (InStruct)
		{
			Metadata->SetMetaData(MetaKey, InStruct->GetPathName());
			Metadata->SetMetaData(ModeKey, Mode == EFunctionPinBaseStructMode::Auto ? FString(TEXT("Auto")) : FString(TEXT("Manual")));
		}
		else
		{
			Metadata->RemoveMetaData(MetaKey);
			Metadata->RemoveMetaData(ModeKey);
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}

	const FStructProperty* FindCustomizationStructFunctionSelfProperty(const UFunction* Function)
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

	UScriptStruct* ResolveCustomizationStructFunctionOwnerFromFunction(const UFunction* Function)
	{
		if (!Function)
		{
			return nullptr;
		}

		if (const FStructProperty* SelfProperty = FindCustomizationStructFunctionSelfProperty(Function))
		{
			return SelfProperty->Struct;
		}

		const FString OwnerMeta = Function->GetMetaData(TEXT("StructFunctionOwner"));
		return ResolveStructFromPath(OwnerMeta);
	}

	UScriptStruct* ResolveBaseStructFromProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return nullptr;
		}

		const FString BaseStructPath = Property->GetMetaData(BaseStructMetaKey);
		return ResolveStructFromPath(BaseStructPath);
	}

	bool IsCompatible(const UScriptStruct* A, const UScriptStruct* B)
	{
		return A && B && (A->IsChildOf(B) || B->IsChildOf(A));
	}

	UScriptStruct* FindLowestCommonStruct(UScriptStruct* A, UScriptStruct* B)
	{
		if (!A || !B)
		{
			return nullptr;
		}

		for (UScriptStruct* Candidate = A; Candidate; Candidate = Cast<UScriptStruct>(Candidate->GetSuperStruct()))
		{
			if (B->IsChildOf(Candidate))
			{
				return Candidate;
			}
		}

		return nullptr;
	}

	void CollectConnectedPins(const UEdGraphPin* SourcePin, TArray<const UEdGraphPin*>& OutPins)
	{
		if (!SourcePin)
		{
			return;
		}

		TArray<const UEdGraphPin*> Queue;
		TSet<const UEdGraphPin*> Visited;
		Queue.Add(SourcePin);

		while (Queue.Num() > 0)
		{
			const UEdGraphPin* Pin = Queue.Pop(EAllowShrinking::No);
			if (!Pin || Visited.Contains(Pin))
			{
				continue;
			}
			Visited.Add(Pin);
			OutPins.Add(Pin);

			if (UEdGraphPin* NetPin = FEdGraphUtilities::GetNetFromPin(const_cast<UEdGraphPin*>(Pin)))
			{
				if (!Visited.Contains(NetPin))
				{
					Queue.Add(NetPin);
				}
			}

			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!Visited.Contains(LinkedPin))
				{
					Queue.Add(LinkedPin);
				}
			}
		}
	}

	void CollectConstraintStructsFromPin(const UEdGraphPin* Pin, TArray<UScriptStruct*>& OutStructs)
	{
		if (!Pin)
		{
			return;
		}

		auto AddStructUnique = [&OutStructs](UScriptStruct* Struct)
		{
			if (Struct)
			{
				OutStructs.AddUnique(Struct);
			}
		};

		if (const UK2Node_CallStructFunctionInstanced* StructCallNode = Cast<UK2Node_CallStructFunctionInstanced>(Pin->GetOwningNode()))
		{
			if (Pin == StructCallNode->GetInstancedTargetPin())
			{
				AddStructUnique(ResolveCustomizationStructFunctionOwnerFromFunction(StructCallNode->GetTargetFunction()));
			}
		}

		if (const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Pin->GetOwningNode()))
		{
			AddStructUnique(ResolveBaseStructFromProperty(VariableNode->GetPropertyForVariable()));
		}

		if (const UK2Node_CallFunction* FunctionNode = Cast<UK2Node_CallFunction>(Pin->GetOwningNode()))
		{
			const UFunction* TargetFunction = FunctionNode->GetTargetFunction();
			if (TargetFunction)
			{
				if (TargetFunction->GetOuterUClass() == UBlueprintInstancedStructLibrary::StaticClass()
					&& TargetFunction->GetFName() == GET_FUNCTION_NAME_CHECKED(UBlueprintInstancedStructLibrary, MakeInstancedStruct))
				{
					if (Pin->Direction == EGPD_Output && Pin->PinName == UEdGraphSchema_K2::PN_ReturnValue)
					{
						if (const UEdGraphPin* ValuePin = FunctionNode->FindPin(TEXT("Value")))
						{
							if (ValuePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
							{
								AddStructUnique(Cast<UScriptStruct>(ValuePin->PinType.PinSubCategoryObject.Get()));
							}
						}
					}
				}

				if (Pin->Direction == EGPD_Output && Pin->PinName == UEdGraphSchema_K2::PN_ReturnValue)
				{
					if (const FProperty* ReturnProperty = TargetFunction->GetReturnProperty())
					{
						if (const FStructProperty* ReturnStructProperty = CastField<FStructProperty>(ReturnProperty))
						{
							if (ReturnStructProperty->Struct == FInstancedStruct::StaticStruct())
							{
								AddStructUnique(ResolveBaseStructFromProperty(ReturnProperty));
							}
						}
					}
				}

				if (Pin->Direction == EGPD_Input)
				{
					if (const FProperty* ParamProperty = FindFProperty<FProperty>(TargetFunction, Pin->PinName))
					{
						if (const FStructProperty* ParamStructProperty = CastField<FStructProperty>(ParamProperty))
						{
							if (ParamStructProperty->Struct == FInstancedStruct::StaticStruct())
							{
								AddStructUnique(ResolveBaseStructFromProperty(ParamProperty));
							}
						}
					}
				}
			}
		}
	}

	void CollectInternalConstraintStructs(const UEdGraphPin* SourcePin, TArray<UScriptStruct*>& OutStructs)
	{
		if (!SourcePin)
		{
			return;
		}

		TArray<const UEdGraphPin*> ConnectedPins;
		CollectConnectedPins(SourcePin, ConnectedPins);
		for (const UEdGraphPin* Pin : ConnectedPins)
		{
			CollectConstraintStructsFromPin(Pin, OutStructs);
		}
	}

	FInferredConstraint InferConstraintFromInternalUsage(const UEdGraphPin* SourcePin)
	{
		FInferredConstraint Result;

		TArray<UScriptStruct*> Constraints;
		CollectInternalConstraintStructs(SourcePin, Constraints);
		if (Constraints.Num() == 0)
		{
			return Result;
		}

		Result.bHasAny = true;

		UScriptStruct* CommonStruct = Constraints[0];
		for (int32 Index = 1; Index < Constraints.Num(); ++Index)
		{
			CommonStruct = FindLowestCommonStruct(CommonStruct, Constraints[Index]);
			if (!CommonStruct)
			{
				Result.bConflict = true;
				return Result;
			}
		}

		Result.Struct = CommonStruct;
		return Result;
	}

	bool IsPinReachableFromSource(const UEdGraphPin* SourcePin, const UEdGraphPin* TargetPin, const UEdGraphPin* ExcludedPin)
	{
		if (!SourcePin || !TargetPin || SourcePin == ExcludedPin || TargetPin == ExcludedPin)
		{
			return false;
		}

		TArray<const UEdGraphPin*> Queue;
		TSet<const UEdGraphPin*> Visited;
		Queue.Add(SourcePin);

		while (Queue.Num() > 0)
		{
			const UEdGraphPin* Pin = Queue.Pop(EAllowShrinking::No);
			if (!Pin || Pin == ExcludedPin || Visited.Contains(Pin))
			{
				continue;
			}
			if (Pin == TargetPin)
			{
				return true;
			}

			Visited.Add(Pin);

			if (UEdGraphPin* NetPin = FEdGraphUtilities::GetNetFromPin(const_cast<UEdGraphPin*>(Pin)))
			{
				if (!Visited.Contains(NetPin) && NetPin != ExcludedPin)
				{
					Queue.Add(NetPin);
				}
			}

			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!Visited.Contains(LinkedPin) && LinkedPin != ExcludedPin)
				{
					Queue.Add(LinkedPin);
				}
			}
		}

		return false;
	}

	int32 DisconnectIncompatibleInternalLinks(const UEdGraphPin* SourcePin, const UScriptStruct* ExpectedStruct)
	{
		if (!SourcePin || !ExpectedStruct)
		{
			return 0;
		}

		TArray<const UEdGraphPin*> ConnectedPins;
		CollectConnectedPins(SourcePin, ConnectedPins);

		TArray<TPair<UEdGraphPin*, UEdGraphPin*>> LinksToBreak;
		auto AddLinkToBreak = [&LinksToBreak](UEdGraphPin* A, UEdGraphPin* B)
		{
			if (!A || !B)
			{
				return;
			}
			const bool bExists = LinksToBreak.ContainsByPredicate([A, B](const TPair<UEdGraphPin*, UEdGraphPin*>& Pair)
			{
				return (Pair.Key == A && Pair.Value == B) || (Pair.Key == B && Pair.Value == A);
			});
			if (!bExists)
			{
				LinksToBreak.Add(TPair<UEdGraphPin*, UEdGraphPin*>(A, B));
			}
		};

		for (const UEdGraphPin* Pin : ConnectedPins)
		{
			if (!Pin || Pin == SourcePin)
			{
				continue;
			}

			TArray<UScriptStruct*> PinConstraints;
			CollectConstraintStructsFromPin(Pin, PinConstraints);

			for (UScriptStruct* ConstraintStruct : PinConstraints)
			{
				if (ConstraintStruct && !IsCompatible(ConstraintStruct, ExpectedStruct))
				{
					UEdGraphPin* MutablePin = const_cast<UEdGraphPin*>(Pin);
					const TArray<UEdGraphPin*> LinkedPins = MutablePin->LinkedTo;
					for (UEdGraphPin* LinkedPin : LinkedPins)
					{
						if (LinkedPin && IsPinReachableFromSource(SourcePin, LinkedPin, Pin))
						{
							AddLinkToBreak(MutablePin, LinkedPin);
						}
					}
					break;
				}
			}
		}

		int32 BrokenLinks = 0;
		for (const TPair<UEdGraphPin*, UEdGraphPin*>& LinkPair : LinksToBreak)
		{
			UEdGraphPin* PinA = LinkPair.Key;
			UEdGraphPin* PinB = LinkPair.Value;
			if (!PinA || !PinB || !PinA->LinkedTo.Contains(PinB))
			{
				continue;
			}

			if (UEdGraphNode* OwningNodeA = PinA->GetOwningNode())
			{
				OwningNodeA->Modify();
			}
			if (UEdGraphNode* OwningNodeB = PinB->GetOwningNode())
			{
				OwningNodeB->Modify();
			}

			PinA->BreakLinkTo(PinB);
			++BrokenLinks;
		}

		return BrokenLinks;
	}

	void GetInstancedStructPins(const UK2Node_EditablePinBase* FunctionNode, EEdGraphPinDirection PinDirection, TArray<const UEdGraphPin*>& OutPins)
	{
		if (!FunctionNode)
		{
			return;
		}

		for (const UEdGraphPin* Pin : FunctionNode->Pins)
		{
			if (!Pin || Pin->Direction != PinDirection)
			{
				continue;
			}
			if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct)
			{
				continue;
			}
			if (Pin->PinType.PinSubCategoryObject != FInstancedStruct::StaticStruct())
			{
				continue;
			}
			if (Pin->PinName == UEdGraphSchema_K2::PN_Execute || Pin->PinName == UEdGraphSchema_K2::PN_Then)
			{
				continue;
			}
			OutPins.Add(Pin);
		}
	}

	class FStructFunctionBaseStructFilter : public IStructViewerFilter
	{
	public:
		virtual bool IsStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const UScriptStruct* InStruct, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
		{
			if (!InStruct)
			{
				return false;
			}

			static const FName HiddenMetaTag = TEXT("Hidden");
			return !InStruct->HasMetaData(HiddenMetaTag);
		}

		virtual bool IsUnloadedStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const FSoftObjectPath& InStructPath, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
		{
			return true;
		}
	};
}

TSharedPtr<IDetailCustomization> FStructFunctionInstancedBaseStructCustomization::MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor)
{
	if (!InBlueprintEditor.IsValid())
	{
		return nullptr;
	}

	const TArray<UObject*>* EditedObjects = InBlueprintEditor->GetObjectsCurrentlyBeingEdited();
	if (!EditedObjects || EditedObjects->IsEmpty())
	{
		return nullptr;
	}

	UBlueprint* EditedBlueprint = Cast<UBlueprint>(EditedObjects->Last());
	if (!EditedBlueprint)
	{
		return nullptr;
	}

	return MakeShared<FStructFunctionInstancedBaseStructCustomization>(EditedBlueprint, InBlueprintEditor);
}

FStructFunctionInstancedBaseStructCustomization::FStructFunctionInstancedBaseStructCustomization(UBlueprint* InBlueprint, TSharedPtr<IBlueprintEditor> InBlueprintEditor)
	: Blueprint(InBlueprint)
	, BlueprintEditor(InBlueprintEditor)
{
}

void FStructFunctionInstancedBaseStructCustomization::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	bCanCustomize = false;
	VariableName = NAME_None;
	VariableScopeFunction = nullptr;

	if (!Blueprint.IsValid())
	{
		return;
	}

	TArray<TWeakObjectPtr<UObject>> EditingObjects;
	InDetailLayout.GetObjectsBeingCustomized(EditingObjects);
	if (EditingObjects.IsEmpty())
	{
		return;
	}

	bool bHasVariableContextObject = false;
	for (const TWeakObjectPtr<UObject>& EditingObject : EditingObjects)
	{
		if (Cast<UK2Node_Variable>(EditingObject.Get()) || Cast<UPropertyWrapper>(EditingObject.Get()))
		{
			bHasVariableContextObject = true;
			break;
		}
	}

	for (const TWeakObjectPtr<UObject>& EditingObject : EditingObjects)
	{
		if (UK2Node_EditablePinBase* EntryNode = Cast<UK2Node_EditablePinBase>(EditingObject.Get()))
		{
			if (Cast<UK2Node_FunctionEntry>(EntryNode) || (Cast<UK2Node_Tunnel>(EntryNode) && EntryNode->DrawNodeAsEntry()))
			{
				CustomizeFunctionPinBaseStruct(InDetailLayout, EntryNode);
				return;
			}
		}
	}

	if (!bHasVariableContextObject)
	{
		if (TSharedPtr<IBlueprintEditor> PinnedBlueprintEditor = BlueprintEditor.Pin())
		{
			if (UEdGraph* FocusedGraph = PinnedBlueprintEditor->GetFocusedGraph())
			{
				if (UK2Node_EditablePinBase* EntryNode = FBlueprintEditorUtils::GetEntryNode(FocusedGraph))
				{
					if (Cast<UK2Node_FunctionEntry>(EntryNode) || (Cast<UK2Node_Tunnel>(EntryNode) && EntryNode->DrawNodeAsEntry()))
					{
						CustomizeFunctionPinBaseStruct(InDetailLayout, EntryNode);
						return;
					}
				}
			}
		}
	}

	FProperty* Property = nullptr;
	if (UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(EditingObjects[0].Get()))
	{
		Property = VariableNode->GetPropertyForVariable();
	}
	else if (UPropertyWrapper* PropertyWrapper = Cast<UPropertyWrapper>(EditingObjects[0].Get()))
	{
		Property = PropertyWrapper->GetProperty();
	}

	FStructProperty* StructProperty = CastField<FStructProperty>(Property);
	if (!StructProperty || StructProperty->Struct != FInstancedStruct::StaticStruct())
	{
		return;
	}

	VariableName = StructProperty->GetFName();
	if (UFunction* OwningFunction = StructProperty->GetOwner<UFunction>())
	{
		if (StructProperty->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			return;
		}
		VariableScopeFunction = OwningFunction;
	}
	else if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint.Get(), VariableName) == INDEX_NONE)
	{
		return;
	}

	bCanCustomize = true;

	IDetailCategoryBuilder& VariableCategory = InDetailLayout.EditCategory(TEXT("Variable"));
	VariableCategory.AddCustomRow(LOCTEXT("BaseStructFilterString", "Base Struct"), true)
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("BaseStructLabel", "Base Struct"))
		.ToolTipText(LOCTEXT("BaseStructTooltip", "Set BaseStruct metadata for this FInstancedStruct blueprint variable."))
		.Font(InDetailLayout.GetDetailFont())
	]
	.ValueContent()
	.MinDesiredWidth(320.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SComboButton)
			.ContentPadding(2.0f)
			.OnGetMenuContent(this, &FStructFunctionInstancedBaseStructCustomization::GenerateStructPickerMenu)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					return GetCurrentBaseStructText(VariableName, VariableScopeFunction.Get());
				})
				.Font(InDetailLayout.GetDetailFont())
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(6.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ClearBaseStruct", "Clear"))
			.ToolTipText(LOCTEXT("ClearBaseStructTooltip", "Remove BaseStruct metadata from this variable."))
			.OnClicked(this, &FStructFunctionInstancedBaseStructCustomization::OnClearBaseStructClicked)
			.IsEnabled_Lambda([this]()
			{
				return ResolveCurrentBaseStruct(VariableName, VariableScopeFunction.Get()) != nullptr;
			})
		]
	];
}

UScriptStruct* FStructFunctionInstancedBaseStructCustomization::ResolveCurrentBaseStruct(const FName& InVariableName, const UFunction* InLocalVarScope) const
{
	if (!Blueprint.IsValid() || !bCanCustomize)
	{
		return nullptr;
	}

	FString BaseStructPath;
	if (!FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint.Get(), InVariableName, InLocalVarScope, BaseStructMetaKey, BaseStructPath)
		|| BaseStructPath.IsEmpty())
	{
		return nullptr;
	}

	return ResolveStructFromPath(BaseStructPath);
}

void FStructFunctionInstancedBaseStructCustomization::SetBaseStructMeta(const FName& InVariableName, const UFunction* InLocalVarScope, const UScriptStruct* InStruct) const
{
	if (!Blueprint.IsValid() || !bCanCustomize)
	{
		return;
	}

	if (InStruct)
	{
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint.Get(), InVariableName, InLocalVarScope, BaseStructMetaKey, InStruct->GetPathName());
	}
	else
	{
		FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint.Get(), InVariableName, InLocalVarScope, BaseStructMetaKey);
	}
}

FText FStructFunctionInstancedBaseStructCustomization::GetCurrentBaseStructText(const FName& InVariableName, const UFunction* InLocalVarScope) const
{
	if (const UScriptStruct* BaseStruct = ResolveCurrentBaseStruct(InVariableName, InLocalVarScope))
	{
		return BaseStruct->GetDisplayNameText();
	}

	return LOCTEXT("NoBaseStruct", "None");
}

TSharedRef<SWidget> FStructFunctionInstancedBaseStructCustomization::GenerateStructPickerMenu()
{
	FStructViewerInitializationOptions Options;
	Options.bShowNoneOption = true;
	Options.NameTypeToDisplay = EStructViewerNameTypeToDisplay::DisplayName;
	Options.DisplayMode = EStructViewerDisplayMode::TreeView;
	Options.bExpandRootNodes = true;
	Options.StructFilter = MakeShared<FStructFunctionBaseStructFilter>();
	Options.SelectedStruct = ResolveCurrentBaseStruct(VariableName, VariableScopeFunction.Get());

	return SNew(SBox)
		.WidthOverride(320.0f)
		.MaxDesiredHeight(500.0f)
		[
			FModuleManager::LoadModuleChecked<FStructViewerModule>("StructViewer")
			.CreateStructViewer(Options, FOnStructPicked::CreateSP(this, &FStructFunctionInstancedBaseStructCustomization::HandleStructPicked))
		];
}

void FStructFunctionInstancedBaseStructCustomization::HandleStructPicked(const UScriptStruct* InStruct)
{
	SetBaseStructMeta(VariableName, VariableScopeFunction.Get(), InStruct);
}

FReply FStructFunctionInstancedBaseStructCustomization::OnClearBaseStructClicked()
{
	SetBaseStructMeta(VariableName, VariableScopeFunction.Get(), nullptr);
	return FReply::Handled();
}

void FStructFunctionInstancedBaseStructCustomization::CustomizeFunctionPinBaseStruct(IDetailLayoutBuilder& InDetailLayout, UK2Node_EditablePinBase* EntryNode)
{
	if (!EntryNode || !Blueprint.IsValid())
	{
		return;
	}

	bCanCustomize = true;

	UK2Node_EditablePinBase* ResultNode = nullptr;
	if (UEdGraph* FunctionGraph = EntryNode->GetGraph())
	{
		TWeakObjectPtr<UK2Node_EditablePinBase> EntryNodeFromGraph;
		TWeakObjectPtr<UK2Node_EditablePinBase> ResultNodeFromGraph;
		FBlueprintEditorUtils::GetEntryAndResultNodes(FunctionGraph, EntryNodeFromGraph, ResultNodeFromGraph);
		if (EntryNodeFromGraph.IsValid())
		{
			EntryNode = EntryNodeFromGraph.Get();
		}
		ResultNode = ResultNodeFromGraph.Get();
	}

	auto BuildPinRow = [this, &InDetailLayout, EntryNode](IDetailCategoryBuilder& Category, const UEdGraphPin* Pin, const FText& TooltipText)
	{
		const FName PinName = Pin->PinName;
		TWeakObjectPtr<UK2Node_EditablePinBase> WeakEntryNode = EntryNode;

		const EFunctionPinBaseStructMode Mode = GetConfiguredPinMode(EntryNode, PinName);
		UScriptStruct* ConfiguredStruct = GetConfiguredPinBaseStruct(EntryNode, PinName);
		const FInferredConstraint Inferred = InferConstraintFromInternalUsage(Pin);
		const UScriptStruct* EffectiveAutoStruct = (Mode == EFunctionPinBaseStructMode::Manual || Inferred.bConflict) ? nullptr : Inferred.Struct;
		const UScriptStruct* DisplayStruct = ConfiguredStruct ? ConfiguredStruct : EffectiveAutoStruct;
		const bool bHasConfiguredStruct = ConfiguredStruct != nullptr;

		const bool bManualConflict = (Mode == EFunctionPinBaseStructMode::Manual)
			&& ConfiguredStruct
			&& Inferred.Struct
			&& !IsCompatible(ConfiguredStruct, Inferred.Struct);

		Category.AddCustomRow(FText::FromName(PinName), false)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("FunctionPinBaseStructLabel", "Base Struct ({0})"), FText::FromName(PinName)))
			.ToolTipText(TooltipText)
			.Font(InDetailLayout.GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(320.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SComboButton)
					.ContentPadding(2.0f)
					.OnGetMenuContent_Lambda([this, WeakEntryNode, PinName, DisplayStruct]()
					{
						FStructViewerInitializationOptions Options;
						Options.bShowNoneOption = true;
						Options.NameTypeToDisplay = EStructViewerNameTypeToDisplay::DisplayName;
						Options.DisplayMode = EStructViewerDisplayMode::TreeView;
						Options.bExpandRootNodes = true;
						Options.StructFilter = MakeShared<FStructFunctionBaseStructFilter>();
						Options.SelectedStruct = DisplayStruct;

						return SNew(SBox)
							.WidthOverride(320.0f)
							.MaxDesiredHeight(500.0f)
							[
								FModuleManager::LoadModuleChecked<FStructViewerModule>("StructViewer")
								.CreateStructViewer(Options, FOnStructPicked::CreateLambda([this, WeakEntryNode, PinName](const UScriptStruct* PickedStruct)
								{
									if (UK2Node_EditablePinBase* TypedEntryNode = WeakEntryNode.Get())
									{
										SetConfiguredPinBaseStruct(TypedEntryNode, Blueprint.Get(), PinName, PickedStruct, EFunctionPinBaseStructMode::Manual);
									}
								}))
							];
					})
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text_Lambda([DisplayStruct]()
						{
							if (DisplayStruct)
							{
								return DisplayStruct->GetDisplayNameText();
							}
							return LOCTEXT("NoBaseStruct", "None");
						})
						.Font(InDetailLayout.GetDetailFont())
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearFunctionPinBaseStruct", "Clear"))
					.OnClicked_Lambda([this, WeakEntryNode, PinName]()
					{
						if (UK2Node_EditablePinBase* TypedEntryNode = WeakEntryNode.Get())
						{
							SetConfiguredPinBaseStruct(TypedEntryNode, Blueprint.Get(), PinName, nullptr, EFunctionPinBaseStructMode::None);
						}
						return FReply::Handled();
					})
					.IsEnabled_Lambda([bHasConfiguredStruct]()
					{
						return bHasConfiguredStruct;
					})
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([WeakEntryNode, PinName, Inferred, bManualConflict, EffectiveAutoStruct]()
					{
						if (Inferred.bConflict)
						{
							return LOCTEXT("InternalConflictText", "Internal usage has conflicting BaseStruct constraints.");
						}
						if (bManualConflict)
						{
							return LOCTEXT("ManualConflictText", "Manual BaseStruct conflicts with internal usage.");
						}
						const EFunctionPinBaseStructMode CurrentMode = GetConfiguredPinMode(WeakEntryNode.Get(), PinName);
						if (CurrentMode == EFunctionPinBaseStructMode::Auto)
						{
							return LOCTEXT("AutoModeText", "Auto synced from internal usage.");
						}
						if (CurrentMode == EFunctionPinBaseStructMode::Manual)
						{
							return LOCTEXT("ManualModeText", "Manual override enabled.");
						}
						if (EffectiveAutoStruct)
						{
							return LOCTEXT("ImplicitAutoModeText", "Auto inferred from internal usage (not pinned yet).");
						}
						return FText::GetEmpty();
					})
					.ColorAndOpacity_Lambda([Inferred, bManualConflict]()
					{
						if (Inferred.bConflict || bManualConflict)
						{
							return FSlateColor(FLinearColor(1.0f, 0.2f, 0.2f));
						}
						return FSlateColor::UseSubduedForeground();
					})
					.Font(InDetailLayout.GetDetailFont())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("KeepInternalButton", "Keep Internal"))
					.Visibility((bManualConflict && Inferred.Struct) ? EVisibility::Visible : EVisibility::Collapsed)
					.OnClicked_Lambda([this, WeakEntryNode, PinName, Inferred]()
					{
						if (UK2Node_EditablePinBase* TypedEntryNode = WeakEntryNode.Get())
						{
							SetConfiguredPinBaseStruct(TypedEntryNode, Blueprint.Get(), PinName, Inferred.Struct, EFunctionPinBaseStructMode::Auto);
						}
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("KeepExternalButton", "Keep External"))
					.Visibility(bManualConflict ? EVisibility::Visible : EVisibility::Collapsed)
					.OnClicked_Lambda([this, WeakEntryNode, PinName, ConfiguredStruct, Pin]()
					{
						if (UK2Node_EditablePinBase* TypedEntryNode = WeakEntryNode.Get())
						{
							SetConfiguredPinBaseStruct(TypedEntryNode, Blueprint.Get(), PinName, ConfiguredStruct, EFunctionPinBaseStructMode::Manual);
							if (DisconnectIncompatibleInternalLinks(Pin, ConfiguredStruct) > 0)
							{
								FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint.Get());
							}
						}
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("AdoptInternalButton", "Adopt Internal"))
					.Visibility((Mode != EFunctionPinBaseStructMode::Manual && EffectiveAutoStruct && !ConfiguredStruct) ? EVisibility::Visible : EVisibility::Collapsed)
					.OnClicked_Lambda([this, WeakEntryNode, PinName, EffectiveAutoStruct]()
					{
						if (UK2Node_EditablePinBase* TypedEntryNode = WeakEntryNode.Get())
						{
							SetConfiguredPinBaseStruct(TypedEntryNode, Blueprint.Get(), PinName, EffectiveAutoStruct, EFunctionPinBaseStructMode::Auto);
						}
						return FReply::Handled();
					})
				]
			]
		];
	};

	TArray<const UEdGraphPin*> InputPins;
	GetInstancedStructPins(EntryNode, EGPD_Output, InputPins);
	if (InputPins.Num() > 0)
	{
		IDetailCategoryBuilder& InputsCategory = InDetailLayout.EditCategory(TEXT("Inputs"));
		for (const UEdGraphPin* Pin : InputPins)
		{
			BuildPinRow(InputsCategory, Pin, LOCTEXT("InputPinBaseStructTooltip", "Set BaseStruct metadata for this FInstancedStruct input parameter."));
		}
	}

	if (ResultNode)
	{
		TArray<const UEdGraphPin*> OutputPins;
		GetInstancedStructPins(ResultNode, EGPD_Input, OutputPins);
		if (OutputPins.Num() > 0)
		{
			IDetailCategoryBuilder& OutputsCategory = InDetailLayout.EditCategory(TEXT("Outputs"));
			for (const UEdGraphPin* Pin : OutputPins)
			{
				BuildPinRow(OutputsCategory, Pin, LOCTEXT("OutputPinBaseStructTooltip", "Set BaseStruct metadata for this FInstancedStruct output parameter."));
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
