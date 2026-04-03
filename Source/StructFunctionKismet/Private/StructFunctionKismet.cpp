// Copyright 2024-2025 Evianaive. All Rights Reserved.

#include "StructFunctionKismet.h"

#include "BlueprintActionDatabase.h"
#include "BlueprintEditorModule.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Tunnel.h"
#include "Misc/CoreDelegates.h"
#include "StructFunctionInstancedBaseStructCustomization.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "FStructFunctionKismetModule"

void FStructFunctionKismetModule::StartupModule()
{
#if WITH_EDITOR
	ModulesChangedHandle = FModuleManager::Get().OnModulesChanged().AddRaw(this, &FStructFunctionKismetModule::HandleModulesChanged);
	PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FStructFunctionKismetModule::HandlePostEngineInit);
	if (GEditor)
	{
		HandlePostEngineInit();
	}
#endif
}

void FStructFunctionKismetModule::ShutdownModule()
{
#if WITH_EDITOR
	if (VariableCustomizationHandle.IsValid())
	{
		if (FBlueprintEditorModule* BlueprintEditorModule = FModuleManager::GetModulePtr<FBlueprintEditorModule>("Kismet"))
		{
			BlueprintEditorModule->UnregisterVariableCustomization(FStructProperty::StaticClass(), VariableCustomizationHandle);
		}
		VariableCustomizationHandle.Reset();
	}

	if (FunctionCustomizationHandle.IsValid())
	{
		if (FBlueprintEditorModule* BlueprintEditorModule = FModuleManager::GetModulePtr<FBlueprintEditorModule>("Kismet"))
		{
			BlueprintEditorModule->UnregisterFunctionCustomization(UK2Node_FunctionEntry::StaticClass(), FunctionCustomizationHandle);
		}
		FunctionCustomizationHandle.Reset();
	}

	if (MacroCustomizationHandle.IsValid())
	{
		if (FBlueprintEditorModule* BlueprintEditorModule = FModuleManager::GetModulePtr<FBlueprintEditorModule>("Kismet"))
		{
			BlueprintEditorModule->UnregisterFunctionCustomization(UK2Node_Tunnel::StaticClass(), MacroCustomizationHandle);
		}
		MacroCustomizationHandle.Reset();
	}

	if (ModulesChangedHandle.IsValid())
	{
		FModuleManager::Get().OnModulesChanged().Remove(ModulesChangedHandle);
		ModulesChangedHandle.Reset();
	}

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
	RegisterBlueprintCustomizationsIfNeeded();
	FBlueprintActionDatabase::Get().RefreshAll();
#endif
}

#if WITH_EDITOR
void FStructFunctionKismetModule::RegisterBlueprintCustomizationsIfNeeded()
{
	if (VariableCustomizationHandle.IsValid() && FunctionCustomizationHandle.IsValid() && MacroCustomizationHandle.IsValid())
	{
		return;
	}

	if (FBlueprintEditorModule* BlueprintEditorModule = FModuleManager::LoadModulePtr<FBlueprintEditorModule>("Kismet"))
	{
		if (!VariableCustomizationHandle.IsValid())
		{
			VariableCustomizationHandle = BlueprintEditorModule->RegisterVariableCustomization(
				FStructProperty::StaticClass(),
				FOnGetVariableCustomizationInstance::CreateStatic(&FStructFunctionInstancedBaseStructCustomization::MakeInstance));
		}

		if (!FunctionCustomizationHandle.IsValid())
		{
			FunctionCustomizationHandle = BlueprintEditorModule->RegisterFunctionCustomization(
				UK2Node_FunctionEntry::StaticClass(),
				FOnGetFunctionCustomizationInstance::CreateStatic(&FStructFunctionInstancedBaseStructCustomization::MakeInstance));
		}

		if (!MacroCustomizationHandle.IsValid())
		{
			MacroCustomizationHandle = BlueprintEditorModule->RegisterFunctionCustomization(
				UK2Node_Tunnel::StaticClass(),
				FOnGetFunctionCustomizationInstance::CreateStatic(&FStructFunctionInstancedBaseStructCustomization::MakeInstance));
		}
	}
}

void FStructFunctionKismetModule::HandleModulesChanged(FName ModuleName, EModuleChangeReason Reason)
{
	if (Reason == EModuleChangeReason::ModuleLoaded && ModuleName == TEXT("Kismet"))
	{
		RegisterBlueprintCustomizationsIfNeeded();
	}
}
#endif

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FStructFunctionKismetModule, StructFunctionKismet)
