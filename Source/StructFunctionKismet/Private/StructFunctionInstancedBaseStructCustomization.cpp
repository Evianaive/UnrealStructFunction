// Copyright 2024-2025 Evianaive. All Rights Reserved.

#include "StructFunctionInstancedBaseStructCustomization.h"

#include "BlueprintEditor.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
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
	if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint.Get(), VariableName) == INDEX_NONE)
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
				.Text(this, &FStructFunctionInstancedBaseStructCustomization::GetCurrentBaseStructText)
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
				return ResolveCurrentBaseStruct() != nullptr;
			})
		]
	];
}

UScriptStruct* FStructFunctionInstancedBaseStructCustomization::ResolveCurrentBaseStruct() const
{
	if (!Blueprint.IsValid() || !bCanCustomize)
	{
		return nullptr;
	}

	FString BaseStructPath;
	if (!FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint.Get(), VariableName, nullptr, BaseStructMetaKey, BaseStructPath)
		|| BaseStructPath.IsEmpty())
	{
		return nullptr;
	}

	if (UScriptStruct* StructType = UClass::TryFindTypeSlow<UScriptStruct>(BaseStructPath))
	{
		return StructType;
	}

	return Cast<UScriptStruct>(LoadObject<UObject>(nullptr, *BaseStructPath));
}

void FStructFunctionInstancedBaseStructCustomization::SetBaseStructMeta(const UScriptStruct* InStruct) const
{
	if (!Blueprint.IsValid() || !bCanCustomize)
	{
		return;
	}

	if (InStruct)
	{
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint.Get(), VariableName, nullptr, BaseStructMetaKey, InStruct->GetPathName());
	}
	else
	{
		FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint.Get(), VariableName, nullptr, BaseStructMetaKey);
	}
}

FText FStructFunctionInstancedBaseStructCustomization::GetCurrentBaseStructText() const
{
	if (const UScriptStruct* BaseStruct = ResolveCurrentBaseStruct())
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
	Options.DisplayMode = EStructViewerDisplayMode::ListView;
	Options.StructFilter = MakeShared<FStructFunctionBaseStructFilter>();
	Options.SelectedStruct = ResolveCurrentBaseStruct();

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
	SetBaseStructMeta(InStruct);
}

FReply FStructFunctionInstancedBaseStructCustomization::OnClearBaseStructClicked()
{
	SetBaseStructMeta(nullptr);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
