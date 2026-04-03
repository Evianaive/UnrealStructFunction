// Copyright 2024-2025 Evianaive. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IBlueprintEditor;
class SWidget;
class UBlueprint;
class UK2Node_EditablePinBase;
class UScriptStruct;
class UFunction;
class FReply;

class FStructFunctionInstancedBaseStructCustomization : public IDetailCustomization
{
public:
	static TSharedPtr<IDetailCustomization> MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor);

	FStructFunctionInstancedBaseStructCustomization(UBlueprint* InBlueprint, TSharedPtr<IBlueprintEditor> InBlueprintEditor);

	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailLayout) override;

private:
	void CustomizeFunctionPinBaseStruct(IDetailLayoutBuilder& InDetailLayout, UK2Node_EditablePinBase* EntryNode);
	UScriptStruct* ResolveCurrentBaseStruct(const FName& InVariableName, const UFunction* InLocalVarScope) const;
	void SetBaseStructMeta(const FName& InVariableName, const UFunction* InLocalVarScope, const UScriptStruct* InStruct) const;
	FText GetCurrentBaseStructText(const FName& InVariableName, const UFunction* InLocalVarScope) const;
	TSharedRef<SWidget> GenerateStructPickerMenu();
	void HandleStructPicked(const UScriptStruct* InStruct);
	FReply OnClearBaseStructClicked();

	TWeakObjectPtr<UBlueprint> Blueprint;
	TWeakPtr<IBlueprintEditor> BlueprintEditor;
	TWeakObjectPtr<UFunction> VariableScopeFunction;
	FName VariableName;
	bool bCanCustomize = false;
};
