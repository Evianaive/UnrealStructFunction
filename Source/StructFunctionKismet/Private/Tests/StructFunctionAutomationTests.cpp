// Copyright 2024-2025 Evianaive. All Rights Reserved.

#include "CoreMinimal.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintActionFilter.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallStructFunctionInstanced.h"
#include "K2Node_VariableGet.h"
#include "Kismet/BlueprintInstancedStructLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/ObjectKey.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FString BuildSignatureKey(const UFunction* Function)
	{
		FString Signature = Function->HasAnyFunctionFlags(FUNC_Const) ? TEXT("const|") : TEXT("mutable|");
		if (const FProperty* ReturnProp = Function->GetReturnProperty())
		{
			Signature += ReturnProp->GetCPPType(nullptr, CPPF_ArgumentOrReturnValue);
		}
		Signature += TEXT("|");
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
			Signature += Property->GetCPPType(nullptr, CPPF_ArgumentOrReturnValue);
			Signature += TEXT(";");
		}
		return Signature;
	}

	FString BuildStructFunctionKeyForTests(const UFunction* Function)
	{
		const FString OwnerName = Function->GetMetaData(TEXT("StructFunctionOwner"));
		const FString OriginalName = Function->GetMetaData(TEXT("StructFunctionOriginalName"));
		FString SignatureKey = Function->GetMetaData(TEXT("StructFunctionSignature"));
		if (OwnerName.IsEmpty() || OriginalName.IsEmpty())
		{
			return Function->GetPathName();
		}
		if (SignatureKey.IsEmpty())
		{
			SignatureKey = BuildSignatureKey(Function);
		}
		return OwnerName + TEXT("|") + OriginalName + TEXT("|") + SignatureKey;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStructFunctionDuplicateTest, "StructFunction.Functions.DuplicateCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStructFunctionDuplicateTest::RunTest(const FString& Parameters)
{
	TMap<FString, TArray<const UFunction*>> FunctionsByKey;
	int32 StructFunctionCount = 0;

	for (TObjectIterator<UFunction> It; It; ++It)
	{
		UFunction* Function = *It;
		if (!Function->HasMetaData(TEXT("StructFunction")))
		{
			continue;
		}
		StructFunctionCount++;

		if (!Function->GetBoolMetaData(TEXT("BlueprintInternalUseOnly")))
		{
			AddError(FString::Printf(TEXT("StructFunction missing BlueprintInternalUseOnly: %s"), *Function->GetPathName()));
		}

		const FString Key = BuildStructFunctionKeyForTests(Function);
		FunctionsByKey.FindOrAdd(Key).Add(Function);
	}

	if (StructFunctionCount == 0)
	{
		AddError(TEXT("No StructFunction UFunctions found. Expected at least one."));
	}

	for (const TPair<FString, TArray<const UFunction*>>& Pair : FunctionsByKey)
	{
		if (Pair.Value.Num() <= 1)
		{
			continue;
		}
		AddError(FString::Printf(TEXT("Duplicate StructFunction key: %s"), *Pair.Key));
		for (const UFunction* Function : Pair.Value)
		{
			AddError(FString::Printf(TEXT("  %s"), *Function->GetPathName()));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStructFunctionInstancedMetaTest, "StructFunction.Functions.InstancedMeta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStructFunctionInstancedMetaTest::RunTest(const FString& Parameters)
{
	for (TObjectIterator<UFunction> It; It; ++It)
	{
		UFunction* Function = *It;
		if (!Function->HasMetaData(TEXT("StructFunction")))
		{
			continue;
		}

		const FString StaticMeta = Function->GetMetaData(TEXT("StructFunctionStatic"));
		const bool bIsStatic = StaticMeta.Equals(TEXT("true"), ESearchCase::IgnoreCase);
		if (!bIsStatic && !Function->HasMetaData(TEXT("StructFunctionInstanced")))
		{
			AddError(FString::Printf(TEXT("StructFunction missing StructFunctionInstanced: %s"), *Function->GetPathName()));
		}

		FProperty* SelfProperty = nullptr;
		for (TFieldIterator<FProperty> PropIt(Function); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;
			if (Property->HasMetaData(TEXT("StructFunctionSelf")))
			{
				SelfProperty = Property;
				break;
			}
		}

		if (!SelfProperty)
		{
			AddError(FString::Printf(TEXT("StructFunction missing Target param: %s"), *Function->GetPathName()));
			continue;
		}
		if (!SelfProperty->HasAnyPropertyFlags(CPF_ReferenceParm))
		{
			AddError(FString::Printf(TEXT("StructFunction Target param is not ref: %s"), *Function->GetPathName()));
		}
		if (Function->HasAnyFunctionFlags(FUNC_Const) && !SelfProperty->HasAnyPropertyFlags(CPF_ConstParm))
		{
			AddError(FString::Printf(TEXT("StructFunction const Target param missing const flag: %s"), *Function->GetPathName()));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStructFunctionInstancedPinFilterTest, "StructFunction.Functions.InstancedPinFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStructFunctionInstancedPinFilterTest::RunTest(const FString& Parameters)
{
	UClass* ExampleClass = UClass::TryFindTypeSlow<UClass>(TEXT("/Script/UStructWithFunction.StructFunctionInstancedExampleObject"));
	if (!ExampleClass)
	{
		AddError(TEXT("Could not resolve UStructFunctionInstancedExampleObject class."));
		return false;
	}

	FProperty* InstancedProperty = FindFProperty<FProperty>(ExampleClass, TEXT("InstancedScore"));
	if (!InstancedProperty)
	{
		AddError(TEXT("Could not find InstancedScore property."));
		return false;
	}

	UBlueprint* TempBlueprint = FKismetEditorUtilities::CreateBlueprint(
		ExampleClass,
		GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_StructFunctionInstancedTest")),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		FName(TEXT("StructFunctionTests")));
	if (!TempBlueprint)
	{
		AddError(TEXT("Failed to create temporary blueprint for Instanced pin filtering test."));
		return false;
	}

	UEdGraph* TempGraph = TempBlueprint->UbergraphPages.Num() > 0 ? TempBlueprint->UbergraphPages[0] : nullptr;
	if (!TempGraph)
	{
		TempGraph = FBlueprintEditorUtils::CreateNewGraph(TempBlueprint, NAME_None, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddUbergraphPage(TempBlueprint, TempGraph);
	}

	UK2Node_VariableGet* VariableNode = NewObject<UK2Node_VariableGet>(TempGraph);
	TempGraph->AddNode(VariableNode, false, false);
	VariableNode->SetFromProperty(InstancedProperty, true, ExampleClass);
	VariableNode->AllocateDefaultPins();

	UEdGraphPin* InstancedPin = nullptr;
	for (UEdGraphPin* Pin : VariableNode->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Output)
		{
			continue;
		}
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && Pin->PinType.PinSubCategoryObject == FInstancedStruct::StaticStruct())
		{
			InstancedPin = Pin;
			break;
		}
	}

	if (!InstancedPin)
	{
		AddError(TEXT("Failed to create InstancedStruct output pin for InstancedScore."));
		return false;
	}

	FBlueprintActionDatabase& ActionDatabase = FBlueprintActionDatabase::Get();
	ActionDatabase.RefreshAll();
	const FBlueprintActionDatabase::FActionRegistry& Registry = ActionDatabase.GetAllActions();

	auto CountVisibleActionsForPin = [&](UEdGraphPin* SourcePin, TMap<FString, int32>& OutNameCounts, int32& OutVisibleCount)
	{
		OutNameCounts.Reset();
		OutVisibleCount = 0;

		FBlueprintActionFilter Filter;
		Filter.Context.Graphs.Add(TempGraph);
		Filter.Context.Pins.Add(SourcePin);

		TSet<const UBlueprintNodeSpawner*> SeenSpawners;
		for (const TPair<FObjectKey, FBlueprintActionDatabase::FActionList>& RegistryPair : Registry)
		{
			for (UBlueprintNodeSpawner* NodeSpawner : RegistryPair.Value)
			{
				if (!NodeSpawner || NodeSpawner->NodeClass != UK2Node_CallStructFunctionInstanced::StaticClass())
				{
					continue;
				}
				if (SeenSpawners.Contains(NodeSpawner))
				{
					continue;
				}
				SeenSpawners.Add(NodeSpawner);

				FBlueprintActionInfo ActionInfo(FInstancedStruct::StaticStruct(), NodeSpawner);
				if (Filter.IsFiltered(ActionInfo))
				{
					continue;
				}

				UEdGraphNode* TemplateNode = NodeSpawner->GetTemplateNode(TempGraph);
				UK2Node_CallStructFunctionInstanced* InstancedCallNode = Cast<UK2Node_CallStructFunctionInstanced>(TemplateNode);
				if (!InstancedCallNode)
				{
					AddError(TEXT("Instanced StructFunction action spawned unexpected node type."));
					continue;
				}

				UFunction* Function = InstancedCallNode->GetTargetFunction();
				if (!Function)
				{
					AddError(TEXT("Instanced StructFunction action has no target function."));
					continue;
				}

				FString OriginalName = Function->GetMetaData(TEXT("StructFunctionOriginalName"));
				if (OriginalName.IsEmpty())
				{
					OriginalName = Function->GetName();
				}
				OutNameCounts.FindOrAdd(OriginalName)++;
				OutVisibleCount++;
			}
		}
	};

	TMap<FString, int32> VisibleNameCounts;
	int32 VisibleStructFunctionCount = 0;
	CountVisibleActionsForPin(InstancedPin, VisibleNameCounts, VisibleStructFunctionCount);

	TestEqual(TEXT("Visible AssignScore action count"), VisibleNameCounts.FindRef(TEXT("AssignScore")), 1);
	TestEqual(TEXT("Visible EvaluateScore action count"), VisibleNameCounts.FindRef(TEXT("EvaluateScore")), 1);
	TestEqual(TEXT("Visible EvaluateScoreNonConst action count"), VisibleNameCounts.FindRef(TEXT("EvaluateScoreNonConst")), 1);
	TestEqual(TEXT("Hidden SetBase action count"), VisibleNameCounts.FindRef(TEXT("SetBase")), 0);
	TestEqual(TEXT("Hidden AddToBase action count"), VisibleNameCounts.FindRef(TEXT("AddToBase")), 0);
	TestEqual(TEXT("Total visible instanced struct actions"), VisibleStructFunctionCount, 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStructFunctionInstancedMakePinFilterTest, "StructFunction.Functions.InstancedMakePinFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStructFunctionInstancedMakePinFilterTest::RunTest(const FString& Parameters)
{
	UScriptStruct* DerivedStruct = UClass::TryFindTypeSlow<UScriptStruct>(TEXT("/Script/UStructWithFunction.StructFunctionDerived"));
	if (!DerivedStruct)
	{
		AddError(TEXT("Could not resolve FStructFunctionDerived."));
		return false;
	}

	UBlueprint* TempBlueprint = FKismetEditorUtilities::CreateBlueprint(
		UObject::StaticClass(),
		GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_StructFunctionInstancedMakeTest")),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		FName(TEXT("StructFunctionTests")));
	if (!TempBlueprint)
	{
		AddError(TEXT("Failed to create temporary blueprint for Instanced make pin filtering test."));
		return false;
	}

	UEdGraph* TempGraph = TempBlueprint->UbergraphPages.Num() > 0 ? TempBlueprint->UbergraphPages[0] : nullptr;
	if (!TempGraph)
	{
		TempGraph = FBlueprintEditorUtils::CreateNewGraph(TempBlueprint, NAME_None, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddUbergraphPage(TempBlueprint, TempGraph);
	}

	UK2Node_CallFunction* MakeNode = NewObject<UK2Node_CallFunction>(TempGraph);
	TempGraph->AddNode(MakeNode, false, false);
	UFunction* MakeFunction = UBlueprintInstancedStructLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UBlueprintInstancedStructLibrary, MakeInstancedStruct));
	if (!MakeFunction)
	{
		AddError(TEXT("Could not resolve MakeInstancedStruct function."));
		return false;
	}
	MakeNode->SetFromFunction(MakeFunction);
	MakeNode->AllocateDefaultPins();

	UEdGraphPin* ValuePin = MakeNode->FindPin(TEXT("Value"));
	UEdGraphPin* InstancedPin = MakeNode->GetReturnValuePin();
	if (!ValuePin || !InstancedPin)
	{
		AddError(TEXT("Failed to create MakeInstancedStruct node pins."));
		return false;
	}

	ValuePin->PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	ValuePin->PinType.PinSubCategoryObject = DerivedStruct;

	FBlueprintActionDatabase& ActionDatabase = FBlueprintActionDatabase::Get();
	ActionDatabase.RefreshAll();
	const FBlueprintActionDatabase::FActionRegistry& Registry = ActionDatabase.GetAllActions();

	auto CountVisibleActionsForPin = [&](UEdGraphPin* SourcePin, TMap<FString, int32>& OutNameCounts, int32& OutVisibleCount)
	{
		OutNameCounts.Reset();
		OutVisibleCount = 0;

		FBlueprintActionFilter Filter;
		Filter.Context.Graphs.Add(TempGraph);
		Filter.Context.Pins.Add(SourcePin);

		TSet<const UBlueprintNodeSpawner*> SeenSpawners;
		for (const TPair<FObjectKey, FBlueprintActionDatabase::FActionList>& RegistryPair : Registry)
		{
			for (UBlueprintNodeSpawner* NodeSpawner : RegistryPair.Value)
			{
				if (!NodeSpawner || NodeSpawner->NodeClass != UK2Node_CallStructFunctionInstanced::StaticClass())
				{
					continue;
				}
				if (SeenSpawners.Contains(NodeSpawner))
				{
					continue;
				}
				SeenSpawners.Add(NodeSpawner);

				FBlueprintActionInfo ActionInfo(FInstancedStruct::StaticStruct(), NodeSpawner);
				if (Filter.IsFiltered(ActionInfo))
				{
					continue;
				}

				UEdGraphNode* TemplateNode = NodeSpawner->GetTemplateNode(TempGraph);
				UK2Node_CallStructFunctionInstanced* InstancedCallNode = Cast<UK2Node_CallStructFunctionInstanced>(TemplateNode);
				if (!InstancedCallNode)
				{
					AddError(TEXT("Instanced StructFunction action spawned unexpected node type."));
					continue;
				}

				UFunction* Function = InstancedCallNode->GetTargetFunction();
				if (!Function)
				{
					AddError(TEXT("Instanced StructFunction action has no target function."));
					continue;
				}

				FString OriginalName = Function->GetMetaData(TEXT("StructFunctionOriginalName"));
				if (OriginalName.IsEmpty())
				{
					OriginalName = Function->GetName();
				}
				OutNameCounts.FindOrAdd(OriginalName)++;
				OutVisibleCount++;
			}
		}
	};

	TMap<FString, int32> VisibleNameCounts;
	int32 VisibleStructFunctionCount = 0;
	CountVisibleActionsForPin(InstancedPin, VisibleNameCounts, VisibleStructFunctionCount);

	TestEqual(TEXT("Visible AssignScore action count (make)"), VisibleNameCounts.FindRef(TEXT("AssignScore")), 1);
	TestEqual(TEXT("Visible EvaluateScore action count (make)"), VisibleNameCounts.FindRef(TEXT("EvaluateScore")), 1);
	TestEqual(TEXT("Visible EvaluateScoreNonConst action count (make)"), VisibleNameCounts.FindRef(TEXT("EvaluateScoreNonConst")), 1);
	TestEqual(TEXT("Hidden SetBase action count (make)"), VisibleNameCounts.FindRef(TEXT("SetBase")), 0);
	TestEqual(TEXT("Hidden AddToBase action count (make)"), VisibleNameCounts.FindRef(TEXT("AddToBase")), 0);
	TestEqual(TEXT("Total visible instanced struct actions (make)"), VisibleStructFunctionCount, 3);

	ValuePin->PinType.ResetToDefaults();
	ValuePin->PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
	ValuePin->PinType.PinSubCategoryObject = nullptr;

	CountVisibleActionsForPin(InstancedPin, VisibleNameCounts, VisibleStructFunctionCount);
	TestEqual(TEXT("Total visible instanced struct actions (make wildcard)"), VisibleStructFunctionCount, 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
