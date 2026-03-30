// Copyright 2024-2025 Evianaive. All Rights Reserved.

#include "StructFunctionKismet.h"

#include "BlueprintActionDatabase.h"
#include "Misc/CoreDelegates.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "FStructFunctionKismetModule"

void FStructFunctionKismetModule::StartupModule()
{
#if WITH_EDITOR
	PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FStructFunctionKismetModule::HandlePostEngineInit);
#endif
}

void FStructFunctionKismetModule::ShutdownModule()
{
#if WITH_EDITOR
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
#endif
}

void FStructFunctionKismetModule::HandlePostEngineInit()
{
#if WITH_EDITOR
	if (!GEditor)
	{
		return;
	}
	FBlueprintActionDatabase::Get().RefreshAll();
#endif
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FStructFunctionKismetModule, StructFunctionKismet)
