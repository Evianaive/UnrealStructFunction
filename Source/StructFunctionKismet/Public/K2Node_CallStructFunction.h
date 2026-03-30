// Copyright 2024-2025 Evianaive. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallStructFunction.generated.h"

UCLASS()
class STRUCTFUNCTIONKISMET_API UK2Node_CallStructFunction : public UK2Node_CallFunction
{
	GENERATED_BODY()

public:
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual bool IsActionFilteredOut(const FBlueprintActionFilter& Filter) override;
};
