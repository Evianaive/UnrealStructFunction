# StructFunction

[English](README.md) | [简体中文](README.zh-CN.md)

StructFunction makes `USTRUCT` methods feel like first-class Blueprint functions.

## Pain Points This Plugin Solves

- Unreal does not support declaring `UFUNCTION` directly inside `USTRUCT`, while using `UObject` wrappers introduces GC/object overhead for value-like data.
- Blueprint lacked an ergonomic abstraction for polymorphic `FInstancedStruct` calls; this plugin provides that path.
- Type-safe action filtering for `FInstancedStruct` contexts is non-trivial without extra compiler/editor integration.

## What You Get

- `USTRUCTFUNCTION(...)` macro to expose struct methods to Blueprints.
- Polymorphic calls for regular struct pins and `FInstancedStruct` pins.
- Typed action filtering for `FInstancedStruct` contexts (property metadata, make-node type, and function metadata when available).
- Cleaner editor UX for struct function nodes (original names and struct-oriented navigation).

## Quick Start

1. Include the macro header:

```cpp
#include "StructFunctionMacros.h"
```

2. Mark struct methods with `USTRUCTFUNCTION`:

```cpp
USTRUCT(BlueprintType)
struct FScoreBase
{
	GENERATED_BODY()

	USTRUCTFUNCTION(BlueprintCallable, Category="Score|Query")
	virtual int32 EvaluateScore() const;
};

USTRUCT(BlueprintType)
struct FScoreDerived : public FScoreBase
{
	GENERATED_BODY()

	USTRUCTFUNCTION(BlueprintCallable, Category="Score|Query")
	virtual int32 EvaluateScore() const override;
};
```

3. For `FInstancedStruct`, set `BaseStruct` either in C++ or directly in Blueprint variable details:

- In Blueprint variable details (for `FInstancedStruct` variables), use the **Base Struct** picker added by this plugin.
- In C++, you can still write metadata explicitly:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(BaseStruct="/Script/YourModule.ScoreBase"))
FInstancedStruct InstancedScore;
```

4. In Blueprints, drag from a struct (or typed `FInstancedStruct`) pin to call available struct functions.

## Editing `BaseStruct` In Blueprint

1. Add a Blueprint variable of type `Instanced Struct` (`FInstancedStruct`).
2. Select that variable in **My Blueprint**.
3. In Details, expand **Variable > Advanced**.
4. Use the **Base Struct** picker to select a `UScriptStruct`.
5. Use **Clear** to remove the metadata.

This writes Blueprint variable metadata key `BaseStruct`, which is then used by StructFunction node filtering.

## How It Works (High Level)

- **UHT modifier stage:** converts `USTRUCTFUNCTION` declarations into callable UFunctions on generated companion classes and writes metadata used later by Kismet/runtime.
- **Kismet node stage:** custom nodes discover and filter valid actions by struct context; for `FInstancedStruct`, compilation injects helper calls to bind the current struct memory address.
- **Runtime thunk stage:** helper thunk validates compatibility and forwards the correct by-ref target memory to the generated function thunk.

## Notes

- If an `FInstancedStruct` pin has no usable type constraints, instanced struct-function actions are hidden by design.
- Type filtering is strongest when context carries explicit type info (for example `BaseStruct` metadata or a typed `MakeInstancedStruct` source).
