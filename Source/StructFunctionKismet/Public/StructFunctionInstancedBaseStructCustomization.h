// Copyright 2024-2025 Evianaive. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IBlueprintEditor;
class SWidget;
class UBlueprint;
class UScriptStruct;
class FReply;

class FStructFunctionInstancedBaseStructCustomization : public IDetailCustomization
{
public:
	static TSharedPtr<IDetailCustomization> MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor);

	FStructFunctionInstancedBaseStructCustomization(UBlueprint* InBlueprint, TSharedPtr<IBlueprintEditor> InBlueprintEditor);

	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailLayout) override;

private:
	UScriptStruct* ResolveCurrentBaseStruct() const;
	void SetBaseStructMeta(const UScriptStruct* InStruct) const;
	FText GetCurrentBaseStructText() const;
	TSharedRef<SWidget> GenerateStructPickerMenu();
	void HandleStructPicked(const UScriptStruct* InStruct);
	FReply OnClearBaseStructClicked();

	TWeakObjectPtr<UBlueprint> Blueprint;
	TWeakPtr<IBlueprintEditor> BlueprintEditor;
	FName VariableName;
	bool bCanCustomize = false;
};
