# StructFunction

[English](README.md) | [简体中文](README.zh-CN.md)

StructFunction 让 `USTRUCT` 成员函数在蓝图中更接近一等公民体验。

## 解决的痛点

- Unreal 原生不支持在 `USTRUCT` 内直接声明 `UFUNCTION`；若改用 `UObject` 包装又会引入 GC 和对象管理开销（对值语义数据偏重）。
- 蓝图里原本缺少对 `FInstancedStruct` 多态调用的易用封装，本插件顺带补上了这条能力链路。
- `FInstancedStruct` 上下文的类型安全筛选如果不做额外编译器/编辑器接入，很难做到稳定和准确。

## 插件能力

- 提供 `USTRUCTFUNCTION(...)` 宏，将结构体函数暴露到蓝图。
- 同时支持普通 struct pin 与 `FInstancedStruct` pin 的多态调用。
- 对 `FInstancedStruct` 做基于上下文的类型筛选（属性元数据、Make 节点类型、函数元数据）。
- 优化节点编辑器体验（显示原始函数名、面向 struct 的跳转行为）。

## 使用方式

1. 在使用 `USTRUCTFUNCTION` 的头文件中包含宏头：

```cpp
#include "StructFunctionMacros.h"
```

2. 在结构体中声明函数：

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

3. 若使用 `FInstancedStruct`，可在蓝图变量面板或 C++ 中设置 `BaseStruct`：

- 对蓝图变量（`FInstancedStruct`）可直接使用本插件提供的 **Base Struct** 结构体选择器。
- 在 C++ 中也可以继续显式写元数据：

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(BaseStruct="/Script/YourModule.ScoreBase"))
FInstancedStruct InstancedScore;
```

4. 在蓝图中从 struct pin（或带类型信息的 `FInstancedStruct` pin）拉线，即可看到可用函数。

## 在蓝图中编辑 `BaseStruct`

1. 新建一个类型为 `Instanced Struct`（`FInstancedStruct`）的蓝图变量。
2. 在 **My Blueprint** 中选中该变量。
3. 在 Details 面板展开 **Variable > Advanced**。
4. 使用 **Base Struct** 选择器选择目标 `UScriptStruct`。
5. 点击 **Clear** 可清除该设置。

该 UI 会写入蓝图变量元数据键 `BaseStruct`，随后 StructFunction 节点筛选会使用它。

## 背后工作逻辑（简述）

- **UHT 修改阶段：** 将 `USTRUCTFUNCTION` 转换为可调用的 UFunction（挂在生成的伴生类上），并写入后续阶段所需元数据。
- **Kismet 节点阶段：** 自定义节点根据 pin 上下文筛选函数；`FInstancedStruct` 路径在编译期会插入辅助调用，绑定当前结构体内存地址。
- **运行时 thunk 阶段：** 辅助 thunk 做兼容性检查，并把正确的按引用目标内存转交给生成函数 thunk。

## 说明

- 当 `FInstancedStruct` pin 没有可用类型约束信息时，Instanced 版本函数会被隐藏（设计如此）。
- 当上下文携带明确类型信息（如 `BaseStruct` 或已定型的 `MakeInstancedStruct`）时，筛选精度最佳。
