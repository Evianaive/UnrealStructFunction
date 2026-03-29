// Copyright 2024-2025 Evianaive. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallStructFunctionInstanced.generated.h"

UCLASS()
class STRUCTFUNCTIONKISMET_API UK2Node_CallStructFunctionInstanced : public UK2Node_CallFunction
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual bool IsActionFilteredOut(const FBlueprintActionFilter& Filter) override;
	virtual bool IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const override;
	virtual FNodeHandlingFunctor* CreateNodeHandler(FKismetCompilerContext& CompilerContext) const override;

	UEdGraphPin* GetInstancedTargetPin() const;
	UEdGraphPin* GetHiddenTargetPin() const;

private:
	static const FName InstancedTargetPinName;
};
