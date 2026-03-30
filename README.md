# StructFunction

This plugin adds `USTRUCTFUNCTION` to expose struct methods as Blueprint-callable functions.
Functions are surfaced for struct pins based on the owning struct type.

## Usage

1. Include the macros header in any file that uses `USTRUCTFUNCTION`:

```cpp
#include "StructFunctionMacros.h"
```

2. Declare a struct method with `USTRUCTFUNCTION`:

```cpp
USTRUCT(BlueprintType)
struct FMyStruct
{
	GENERATED_BODY()

	USTRUCTFUNCTION(BlueprintCallable, Category="MyStruct")
	int32 Add(int32 A, int32 B) const;
};
```

3. In Blueprints, drag from a `FMyStruct` pin to call the function directly.

## Notes
- The node uses a generated `Target` input pin of the struct type.
- A hidden companion UCLASS is generated per header to host the UFunctions.
- The UHT modifier runs during `Build.bat` (Editor target) and rewrites generated thunks.
