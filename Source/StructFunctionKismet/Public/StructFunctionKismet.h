// Copyright 2024-2025 Evianaive. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FStructFunctionKismetModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void HandlePostEngineInit();

#if WITH_EDITOR
	FDelegateHandle PostEngineInitHandle;
#endif
};
