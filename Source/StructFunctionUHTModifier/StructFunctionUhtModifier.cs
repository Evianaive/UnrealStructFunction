// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Reflection;
using EpicGames.Core;
using EpicGames.UHT.Exporters.CodeGen;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using UnrealBuildTool;

namespace Plugins.StructFunction.StructFunctionUHTModifierUbtPlugin
{
	internal static class StructFunctionConstants
	{
		public const string StructFunctionKey = "StructFunction";
		public const string StructFunctionOwnerKey = "StructFunctionOwner";
		public const string StructFunctionOriginalNameKey = "StructFunctionOriginalName";
		public const string StructFunctionStaticKey = "StructFunctionStatic";
		public const string StructFunctionSelfKey = "StructFunctionSelf";
		public const string StructFunctionTargetParamName = "Target";
		public const string StructFunctionSignatureKey = "StructFunctionSignature";
		public const string StructFunctionInstancedKey = "StructFunctionInstanced";
		public const int GeneratedBodyLineBase = 100000;
	}

	internal sealed class StructFunctionLibraryInfo
	{
		public StructFunctionLibraryInfo(UhtClass classObj, string className, int generatedBodyLineNumber)
		{
			Class = classObj;
			ClassName = className;
			GeneratedBodyLineNumber = generatedBodyLineNumber;
		}

		public UhtClass Class { get; }
		public string ClassName { get; }
		public int GeneratedBodyLineNumber { get; }
	}

	internal sealed class StructFunctionInfo
	{
		public StructFunctionInfo(UhtFunction function, UhtScriptStruct structObj, string originalName, bool isStatic, string signatureKey, StructFunctionLibraryInfo libraryInfo)
		{
			Function = function;
			Struct = structObj;
			OriginalName = originalName;
			IsStatic = isStatic;
			SignatureKey = signatureKey;
			LibraryInfo = libraryInfo;
		}

		public UhtFunction Function { get; }
		public UhtScriptStruct Struct { get; }
		public string OriginalName { get; }
		public bool IsStatic { get; }
		public string SignatureKey { get; }
		public StructFunctionLibraryInfo LibraryInfo { get; }
	}

	internal static class StructFunctionRegistry
	{
		public static readonly List<StructFunctionInfo> Functions = new();
		private static readonly HashSet<string> UsedFunctionNames = new(StringComparer.Ordinal);
		private static readonly Dictionary<UhtHeaderFile, StructFunctionLibraryInfo> LibrariesByHeader = new();
		private static readonly Dictionary<UhtHeaderFile, int> GeneratedLineByHeader = new();

		public static string MakeUniqueFunctionName(UhtScriptStruct structObj, string originalName)
		{
			string baseName = $"{structObj.SourceName}_{originalName}";
			string uniqueName = baseName;
			int index = 1;
			while (!UsedFunctionNames.Add(uniqueName))
			{
				uniqueName = $"{baseName}_{index}";
				index++;
			}
			return uniqueName;
		}

		public static StructFunctionLibraryInfo GetOrCreateLibrary(UhtHeaderFile headerFile, int lineNumber)
		{
			if (LibrariesByHeader.TryGetValue(headerFile, out StructFunctionLibraryInfo? existing))
			{
				MoveLibraryClassToEnd(headerFile, existing.Class);
				return existing;
			}

			int generatedLine = StructFunctionConstants.GeneratedBodyLineBase + (GeneratedLineByHeader.TryGetValue(headerFile, out int current) ? current + 1 : 0);
			GeneratedLineByHeader[headerFile] = generatedLine - StructFunctionConstants.GeneratedBodyLineBase;

			string headerBaseName = Path.GetFileNameWithoutExtension(headerFile.FilePath);
			string className = $"UStructFunctionLibrary_{headerBaseName}_{headerFile.HeaderFileTypeIndex}";

			UhtEngineNameParts nameParts = UhtUtilities.GetEngineNameParts(className);
			UhtClass libraryClass = new(headerFile, headerFile.Module.ScriptPackage, lineNumber)
			{
				ClassType = UhtClassType.Class,
				SourceName = className,
				EngineName = nameParts.EngineName.ToString(),
				PrologLineNumber = lineNumber,
				GeneratedBodyLineNumber = generatedLine,
				GeneratedBodyAccessSpecifier = UhtAccessSpecifier.Public,
				HasGeneratedBody = true,
			};
			libraryClass.ClassFlags |= EClassFlags.Native | EClassFlags.Abstract;
			libraryClass.SuperIdentifier = new UhtToken(UhtTokenType.Identifier, 0, 0, 0, lineNumber, new StringView("UObject"));
			UhtParsingScope.AddModuleRelativePathToMetaData(libraryClass.MetaData, headerFile);
			libraryClass.MetaData.Add(UhtNames.IncludePath, headerFile.ModuleRelativeFilePath);

			headerFile.AddChild(libraryClass);
			MoveLibraryClassToEnd(headerFile, libraryClass);

			StructFunctionLibraryInfo info = new(libraryClass, className, generatedLine);
			LibrariesByHeader[headerFile] = info;
			return info;
		}

		private static void MoveLibraryClassToEnd(UhtHeaderFile headerFile, UhtClass libraryClass)
		{
			if (!TryGetChildrenList(headerFile, out List<UhtType>? children) || children!.Count == 0)
			{
				PromoteLibraryLineNumbers(libraryClass);
				return;
			}
			int index = children.IndexOf(libraryClass);
			if (index < 0 || index == children.Count - 1)
			{
				PromoteLibraryLineNumbers(libraryClass);
				return;
			}
			children.RemoveAt(index);
			children.Add(libraryClass);
			PromoteLibraryLineNumbers(libraryClass);
		}

		private static bool TryGetChildrenList(UhtHeaderFile headerFile, out List<UhtType>? children)
		{
			const BindingFlags flags = BindingFlags.Instance | BindingFlags.NonPublic;
			children = null;
			Type? current = typeof(UhtHeaderFile);
			while (current != null)
			{
				FieldInfo? field = current.GetField("_children", flags);
				if (field != null && field.FieldType == typeof(List<UhtType>))
				{
					children = field.GetValue(headerFile) as List<UhtType>;
					return children != null;
				}

				foreach (FieldInfo candidate in current.GetFields(flags))
				{
					if (candidate.FieldType == typeof(List<UhtType>))
					{
						children = candidate.GetValue(headerFile) as List<UhtType>;
						return children != null;
					}
				}
				current = current.BaseType;
			}
			return false;
		}

		private static void PromoteLibraryLineNumbers(UhtClass libraryClass)
		{
			int promotedLine = Math.Max(libraryClass.PrologLineNumber, libraryClass.GeneratedBodyLineNumber);
			libraryClass.PrologLineNumber = promotedLine;

			const BindingFlags flags = BindingFlags.Instance | BindingFlags.NonPublic;
			Type? current = typeof(UhtType);
			while (current != null)
			{
				FieldInfo? field = current.GetField("_lineNumber", flags);
				if (field != null && field.FieldType == typeof(int))
				{
					int currentLine = (int)field.GetValue(libraryClass)!;
					if (currentLine < promotedLine)
					{
						field.SetValue(libraryClass, promotedLine);
					}
					break;
				}
				current = current.BaseType;
			}
		}

	}

	internal struct StructFunctionAdvancedDisplayParameterHandler
	{
		private readonly UhtMetaData _metaData;
		private readonly string[]? _parameterNames;
		private readonly int _numberLeaveUnmarked;
		private readonly bool _bUseNumber;
		private int _alreadyLeft;

		public StructFunctionAdvancedDisplayParameterHandler(UhtMetaData metaData)
		{
			_metaData = metaData;
			_parameterNames = null;
			_numberLeaveUnmarked = -1;
			_alreadyLeft = 0;
			_bUseNumber = false;

			if (_metaData.TryGetValue(UhtNames.AdvancedDisplay, out string? foundString))
			{
				_parameterNames = foundString.ToString().Split(',', StringSplitOptions.RemoveEmptyEntries);
				for (int index = 0, endIndex = _parameterNames.Length; index < endIndex; ++index)
				{
					_parameterNames[index] = _parameterNames[index].Trim();
				}
				if (_parameterNames.Length == 1)
				{
					_bUseNumber = int.TryParse(_parameterNames[0], out _numberLeaveUnmarked);
				}
			}
		}

		public bool ShouldMarkParameter(StringView parameterName)
		{
			if (_bUseNumber)
			{
				if (_numberLeaveUnmarked < 0)
				{
					return false;
				}
				if (_alreadyLeft < _numberLeaveUnmarked)
				{
					++_alreadyLeft;
					return false;
				}
				return true;
			}

			if (_parameterNames == null)
			{
				return false;
			}

			foreach (string element in _parameterNames)
			{
				if (parameterName.Span.Equals(element, StringComparison.OrdinalIgnoreCase))
				{
					return true;
				}
			}
			return false;
		}

		public bool CanMarkMore()
		{
			return _bUseNumber ? _numberLeaveUnmarked > 0 : (_parameterNames != null && _parameterNames.Length > 0);
		}
	}

	[UnrealHeaderTool]
	public static class UhtStructFunctionParser
	{
		[UhtKeyword(Extends = UhtTableNames.ScriptStruct, Keyword = "USTRUCTFUNCTION")]
		private static UhtParseResult USTRUCTFUNCTIONKeyword(UhtParsingScope parentScope, UhtParsingScope actionScope, ref UhtToken token)
		{
			if (parentScope.ScopeType is not UhtScriptStruct structObj)
			{
				return UhtParseResult.Unhandled;
			}

			StructFunctionLibraryInfo libraryInfo = StructFunctionRegistry.GetOrCreateLibrary(structObj.HeaderFile, token.InputLine);

			UhtFunction function = new(parentScope.HeaderFile, libraryInfo.Class, token.InputLine);
			function.FunctionType = UhtFunctionType.Function;
			function.FunctionFlags |= EFunctionFlags.Native | EFunctionFlags.Public | EFunctionFlags.Static;
			function.FunctionExportFlags |= UhtFunctionExportFlags.CppStatic;

			using UhtParsingScope topScope = new(parentScope, function, parentScope.Session.GetKeywordTable(UhtTableNames.Function), UhtAccessSpecifier.Public);
			using UhtMessageContext tokenContext = new("USTRUCTFUNCTION");
			topScope.AddModuleRelativePathToMetaData();

			UhtSpecifierContext specifierContext = new(topScope, topScope.TokenReader, function.MetaData);
			UhtSpecifierParser specifierParser = UhtSpecifierParser.GetThreadInstance(specifierContext, "USTRUCTFUNCTION", parentScope.Session.GetSpecifierTable(UhtTableNames.Function));
			int specifierStart = topScope.TokenReader.InputPos;
			specifierParser.ParseSpecifiers();
			int specifierEnd = topScope.TokenReader.InputPos;
			if (specifierEnd > specifierStart)
			{
				StringView specifierText = new(topScope.HeaderFile.Data, specifierStart, specifierEnd - specifierStart);
				string specifiers = specifierText.ToString();
				if (specifiers.Contains("BlueprintPure", StringComparison.Ordinal))
				{
					function.FunctionFlags |= EFunctionFlags.BlueprintCallable | EFunctionFlags.BlueprintPure;
				}
				else if (specifiers.Contains("BlueprintCallable", StringComparison.Ordinal))
				{
					function.FunctionFlags |= EFunctionFlags.BlueprintCallable;
				}
			}

			function.MacroLineNumber = topScope.TokenReader.InputLine;
			topScope.TokenReader.OptionalAttributes(false);

			bool isStaticKeyword = topScope.TokenReader.TryOptional("static");
			if (isStaticKeyword)
			{
				function.FunctionFlags |= EFunctionFlags.Static;
				function.FunctionExportFlags |= UhtFunctionExportFlags.CppStatic;
			}
			topScope.TokenReader.TryOptional("virtual");

			UhtToken funcNameToken = new();
			UhtProperty? returnValueProperty = null;
			topScope.HeaderParser.GetCachedPropertyParser().Parse(topScope, EPropertyFlags.None,
				GetPropertyParseOptions(function, true), UhtPropertyCategory.Return,
				(UhtParsingScope topScope, UhtProperty property, ref UhtToken nameToken, UhtLayoutMacroType layoutMacroType) =>
				{
					property.PropertyFlags |= EPropertyFlags.Parm | EPropertyFlags.OutParm | EPropertyFlags.ReturnParm;
					funcNameToken = nameToken;
					if (property is not UhtVoidProperty)
					{
						returnValueProperty = property;
					}
				});

			if (funcNameToken.Value.Length == 0)
			{
				throw new UhtException(topScope.TokenReader, "expected return value and function name");
			}

			string originalName = funcNameToken.Value.ToString();
			string uniqueName = StructFunctionRegistry.MakeUniqueFunctionName(structObj, originalName);
			function.SourceName = uniqueName;

			function.MetaData.Add(StructFunctionConstants.StructFunctionKey, "");
			function.MetaData.Add(StructFunctionConstants.StructFunctionOwnerKey, structObj.SourceName);
			function.MetaData.Add(StructFunctionConstants.StructFunctionOriginalNameKey, originalName);
			function.MetaData.Add(StructFunctionConstants.StructFunctionStaticKey, isStaticKeyword ? "true" : "false");
			if (!isStaticKeyword && !function.MetaData.ContainsKey(StructFunctionConstants.StructFunctionInstancedKey))
			{
				function.MetaData.Add(StructFunctionConstants.StructFunctionInstancedKey, "true");
			}
			if (!function.MetaData.ContainsKey("BlueprintInternalUseOnly"))
			{
				function.MetaData.Add("BlueprintInternalUseOnly", true);
			}
			if (!isStaticKeyword)
			{
				AppendCsvMeta(function.MetaData, "AutoCreateRefTerm", StructFunctionConstants.StructFunctionTargetParamName);
			}
			if (!function.MetaData.ContainsKey(UhtNames.DisplayName))
			{
				function.MetaData.Add(UhtNames.DisplayName, originalName);
			}

			specifierParser.ParseDeferred();
			FinalizeFunctionSpecifiers(function);

			SetFunctionNames(function);
			AddFunction(function);

			topScope.TokenReader.Require('(');

			UhtProperty selfProperty = CreateSelfParameter(function, structObj, token.InputLine, isStaticKeyword);
			function.AddChildDirectly(selfProperty);

			ParseParameterList(topScope, GetPropertyParseOptions(function, false));

			if (returnValueProperty != null)
			{
				topScope.ScopeType.AddChild(returnValueProperty);
			}

			bool isConstMethod = topScope.TokenReader.TryOptional("const");
			if (isConstMethod)
			{
				function.FunctionFlags |= EFunctionFlags.Const;
				function.FunctionExportFlags |= UhtFunctionExportFlags.DeclaredConst;
				selfProperty.PropertyFlags |= EPropertyFlags.ConstParm;
				selfProperty.MetaData.Add(UhtNames.NativeConst, "");
			}

			specifierParser.ParseFieldMetaData();
			topScope.AddFormattedCommentsAsTooltipMetaData();

			if (topScope.TokenReader.TryOptional(';'))
			{
			}
			else if (topScope.TokenReader.TryPeekOptional('{'))
			{
				UhtToken tokenCopy = new();
				topScope.TokenReader.SkipDeclaration(ref tokenCopy);
			}

			string signatureKey = BuildFunctionSignatureKey(function);
			function.MetaData.Add(StructFunctionConstants.StructFunctionSignatureKey, signatureKey);
			StructFunctionInfo info = new(function, structObj, originalName, isStaticKeyword, signatureKey, libraryInfo);
			StructFunctionRegistry.Functions.Add(info);
			return UhtParseResult.Handled;
		}

		private static UhtProperty CreateSelfParameter(UhtFunction function, UhtScriptStruct structObj, int lineNumber, bool isStatic)
		{
			const UhtPropertyCategory propertyCategory = UhtPropertyCategory.RegularParameter;
			EPropertyFlags disallowFlags = ~(EPropertyFlags.ParmFlags | EPropertyFlags.AutoWeak | EPropertyFlags.RepSkip | EPropertyFlags.UObjectWrapper | EPropertyFlags.NativeAccessSpecifiers);

			UhtPropertySettings settings = new();
			settings.Reset(function, lineNumber, propertyCategory, disallowFlags);
			settings.SourceName = StructFunctionConstants.StructFunctionTargetParamName;
			settings.EngineName = StructFunctionConstants.StructFunctionTargetParamName;
			settings.PropertyFlags = EPropertyFlags.Parm | EPropertyFlags.ReferenceParm;
			settings.PropertyExportFlags = UhtPropertyExportFlags.Public;
			settings.PropertyCategory = propertyCategory;
			settings.MetaData.Add(StructFunctionConstants.StructFunctionSelfKey, "true");
			if (isStatic)
			{
				settings.MetaData.Add("StructFunctionStaticTarget", "true");
			}

			UhtStructProperty structProperty = new(settings, structObj);
			return structProperty;
		}

		private static void AppendCsvMeta(UhtMetaData metaData, string key, string value)
		{
			if (metaData.TryGetValue(key, out string? existingValue) && !string.IsNullOrEmpty(existingValue))
			{
				foreach (string entry in existingValue.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
				{
					if (string.Equals(entry, value, StringComparison.Ordinal))
					{
						return;
					}
				}
				metaData.Remove(key);
				metaData.Add(key, existingValue + "," + value);
				return;
			}

			metaData.Add(key, value);
		}

		private static string BuildFunctionSignatureKey(UhtFunction function)
		{
			using BorrowStringBuilder borrower = new(StringBuilderCache.Big);
			StringBuilder builder = borrower.StringBuilder;
			builder.Append(function.FunctionFlags.HasAnyFlags(EFunctionFlags.Const) ? "const" : "mutable");
			builder.Append('|');
			if (function.ReturnProperty != null)
			{
				builder.AppendPropertyText(function.ReturnProperty, UhtPropertyTextType.FunctionThunkParameterArgType);
			}
			builder.Append('|');
			foreach (UhtType parameter in function.ParameterProperties.Span)
			{
				if (parameter is not UhtProperty property)
				{
					continue;
				}
				if (property.MetaData.ContainsKey(StructFunctionConstants.StructFunctionSelfKey))
				{
					continue;
				}
				if (property.PropertyFlags.HasAnyFlags(EPropertyFlags.ReturnParm))
				{
					continue;
				}
				builder.AppendPropertyText(property, UhtPropertyTextType.FunctionThunkParameterArgType);
				builder.Append(';');
			}
			return builder.ToString();
		}




		private static void SetFunctionNames(UhtFunction function)
		{
			string functionName = function.EngineName;
			if (functionName.EndsWith(UhtFunction.GeneratedDelegateSignatureSuffix, StringComparison.Ordinal))
			{
				functionName = functionName[..^UhtFunction.GeneratedDelegateSignatureSuffix.Length];
			}

			function.UnMarshalAndCallName = "exec" + functionName;
			if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent))
			{
				function.MarshalAndCallName = functionName;
				if (function.FunctionFlags.HasAllFlags(EFunctionFlags.BlueprintEvent | EFunctionFlags.Native))
				{
					function.CppImplName = function.EngineName + "_Implementation";
				}
			}
			else if (function.FunctionFlags.HasAllFlags(EFunctionFlags.Native | EFunctionFlags.Net))
			{
				function.MarshalAndCallName = functionName;
				if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetResponse))
				{
					function.CppImplName = function.EngineName;
				}
				else
				{
					if (function.CppImplName.Length == 0)
					{
						function.CppImplName = function.EngineName + "_Implementation";
					}
					else if (function.CppImplName == functionName)
					{
						function.LogError("Native implementation function must be different than original function name.");
					}

					if (function.CppValidationImplName.Length == 0 && function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetValidate))
					{
						function.CppValidationImplName = function.EngineName + "_Validate";
					}
					else if (function.CppValidationImplName == functionName)
					{
						function.LogError("Validation function must be different than original function name.");
					}
				}
			}
			else if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate))
			{
				function.MarshalAndCallName = "delegate" + functionName;
			}

			if (function.CppImplName.Length == 0)
			{
				function.CppImplName = functionName;
			}

			if (function.MarshalAndCallName.Length == 0)
			{
				function.MarshalAndCallName = "event" + functionName;
			}
		}

		private static void AddFunction(UhtFunction function)
		{
			function.Outer?.AddChild(function);
		}

		private static void FinalizeFunctionSpecifiers(UhtFunction function)
		{
			if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
			{
				function.FunctionFlags |= EFunctionFlags.Event;
			}
		}

		private static UhtPropertyParseOptions GetPropertyParseOptions(UhtFunction function, bool returnValue)
		{
			switch (function.FunctionType)
			{
				case UhtFunctionType.Delegate:
				case UhtFunctionType.SparseDelegate:
					return (returnValue ? UhtPropertyParseOptions.None : UhtPropertyParseOptions.CommaSeparatedName) | UhtPropertyParseOptions.DontAddReturn;

				case UhtFunctionType.Function:
					UhtPropertyParseOptions options = UhtPropertyParseOptions.DontAddReturn;
					options |= returnValue ? UhtPropertyParseOptions.FunctionNameIncluded : UhtPropertyParseOptions.NameIncluded;
					if (function.FunctionFlags.HasAllFlags(EFunctionFlags.BlueprintEvent | EFunctionFlags.Native))
					{
						options |= UhtPropertyParseOptions.NoAutoConst;
					}
					return options;

				default:
					throw new UhtIceException("Unknown enumeration value");
			}
		}

		private static void ParseParameterList(UhtParsingScope topScope, UhtPropertyParseOptions options)
		{
			UhtFunction function = (UhtFunction)topScope.ScopeType;

			UhtPropertyCategory propertyCategory = UhtPropertyCategory.RegularParameter;
			EPropertyFlags disallowFlags = ~(EPropertyFlags.ParmFlags | EPropertyFlags.AutoWeak | EPropertyFlags.RepSkip | EPropertyFlags.UObjectWrapper | EPropertyFlags.NativeAccessSpecifiers);
			StructFunctionAdvancedDisplayParameterHandler advancedDisplay = new(topScope.ScopeType.MetaData);

			topScope.TokenReader.RequireList(')', ',', false, () =>
			{
				topScope.HeaderParser.GetCachedPropertyParser().Parse(topScope, disallowFlags, options, propertyCategory,
					(UhtParsingScope topScope, UhtProperty property, ref UhtToken nameToken, UhtLayoutMacroType layoutMacroType) =>
					{
						property.PropertyFlags |= EPropertyFlags.Parm;
						if (advancedDisplay.CanMarkMore() && advancedDisplay.ShouldMarkParameter(property.EngineName))
						{
							property.PropertyFlags |= EPropertyFlags.AdvancedDisplay;
						}

						if (topScope.TokenReader.TryOptional('='))
						{
							List<UhtToken> defaultValueTokens = new();
							int parenthesisNestCount = 0;
							while (!topScope.TokenReader.IsEOF)
							{
								UhtToken token = topScope.TokenReader.PeekToken();
								if (token.IsSymbol(','))
								{
									if (parenthesisNestCount == 0)
									{
										break;
									}
									defaultValueTokens.Add(token);
									topScope.TokenReader.ConsumeToken();
								}
								else if (token.IsSymbol(')'))
								{
									if (parenthesisNestCount == 0)
									{
										break;
									}
									defaultValueTokens.Add(token);
									topScope.TokenReader.ConsumeToken();
									--parenthesisNestCount;
								}
								else if (token.IsSymbol('('))
								{
									++parenthesisNestCount;
									defaultValueTokens.Add(token);
									topScope.TokenReader.ConsumeToken();
								}
								else
								{
									defaultValueTokens.Add(token);
									topScope.TokenReader.ConsumeToken();
								}
							}

						bool storeCppDefaultValueInMetaData = function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable | EFunctionFlags.Exec);
						if (defaultValueTokens.Count > 0 && storeCppDefaultValueInMetaData)
						{
							property.DefaultValueTokens = defaultValueTokens;
						}
					}
				});
			});
		}
	}

	[UnrealHeaderTool]
	class StructFunctionGeneratedCodeModifier
	{
		[UhtExporter(Name = "StructFunction", Description = "Modify generated thunk for USTRUCTFUNCTION",
			Options = UhtExporterOptions.Default, ModuleName = "UStructWithFunction")]
		private static void ScriptGeneratorExporter(IUhtExportFactory factory)
		{
			new StructFunctionGeneratedCodeModifier(factory).Modify();
		}

		private StructFunctionGeneratedCodeModifier(IUhtExportFactory inFactory)
		{
			Factory = inFactory;
		}

		private void Modify()
		{
			if (StructFunctionRegistry.Functions.Count == 0)
			{
				return;
			}

			foreach (UhtModule module in Factory.Session.Modules)
			{
				foreach (UhtHeaderFile headerFile in module.Headers)
				{
					ModifyHeaderGenFile(headerFile);
					ModifyGeneratedHeaderFile(headerFile);
				}
			}
		}

		private void ModifyHeaderGenFile(UhtHeaderFile headerFile)
		{
			string cppFilePath = Path.Combine(headerFile.Module.Module.OutputDirectory, headerFile.FileNameWithoutExtension) + ".gen.cpp";
			if (!File.Exists(cppFilePath))
			{
				return;
			}

			string[] allLines = File.ReadAllLines(cppFilePath);
			Dictionary<string, int> lineIndices = new(StringComparer.Ordinal);

			for (int i = 0; i < allLines.Length; i++)
			{
				string line = allLines[i];
				if (line.Contains("DEFINE_FUNCTION("))
				{
					int funcNameStart = line.IndexOf("::exec", StringComparison.CurrentCulture);
					if (funcNameStart == -1)
					{
						continue;
					}
					string execName = line.Substring(funcNameStart + 6, line.Length - 7 - funcNameStart);
					lineIndices[execName] = i;
				}
			}

			List<(StructFunctionInfo Info, int LineIndex)> targets = new();
			foreach (StructFunctionInfo info in StructFunctionRegistry.Functions)
			{
				if (info.LibraryInfo.Class.HeaderFile != headerFile)
				{
					continue;
				}
				UhtFunction function = info.Function;
				if (!lineIndices.TryGetValue(function.CppImplName, out int lineIndex))
				{
					continue;
				}
				targets.Add((info, lineIndex));
			}

			targets.Sort((left, right) => right.LineIndex.CompareTo(left.LineIndex));
			foreach ((StructFunctionInfo info, int lineIndex) in targets)
			{
				if (!info.IsStatic)
				{
					UhtProperty? selfProperty = FindSelfProperty(info.Function);
					if (selfProperty != null)
					{
						StringBuilder selfNameBuilder = new();
						selfProperty.AppendFunctionThunkParameterName(selfNameBuilder);
						string selfParamName = selfNameBuilder.ToString();

						for (int i = lineIndex + 1; i < allLines.Length; i++)
						{
							if (allLines[i].Contains("P_FINISH"))
							{
								break;
							}
							if (allLines[i].Contains("P_GET_STRUCT(", StringComparison.Ordinal)
								&& allLines[i].Contains(selfParamName, StringComparison.Ordinal))
							{
								allLines[i] = allLines[i].Replace("P_GET_STRUCT(", "P_GET_STRUCT_REF(", StringComparison.Ordinal);
								break;
							}
						}
					}
				}

				for (int i = lineIndex + 1; i < allLines.Length; i++)
				{
					if (allLines[i].Contains("P_NATIVE_END"))
					{
						break;
					}
					if (allLines[i].Contains(info.OriginalName, StringComparison.Ordinal))
					{
						List<string> replacementLines = BuildStructCallLines(info);
						if (replacementLines.Count > 0)
						{
							List<string> updatedLines = new(allLines.Length + replacementLines.Count);
							for (int lineIndexCursor = 0; lineIndexCursor < i; lineIndexCursor++)
							{
								updatedLines.Add(allLines[lineIndexCursor]);
							}
							updatedLines.AddRange(replacementLines);
							for (int lineIndexCursor = i + 1; lineIndexCursor < allLines.Length; lineIndexCursor++)
							{
								updatedLines.Add(allLines[lineIndexCursor]);
							}
							allLines = updatedLines.ToArray();
						}
						break;
					}
				}
			}

			File.WriteAllLines(cppFilePath, allLines);
		}

		private List<string> BuildStructCallLines(StructFunctionInfo info)
		{
			UhtFunction function = info.Function;
			UhtProperty? selfProperty = FindSelfProperty(function);
			if (selfProperty == null)
			{
				return new List<string>();
			}

			StringBuilder selfNameBuilder = new();
			selfProperty.AppendFunctionThunkParameterName(selfNameBuilder);
			string selfName = selfNameBuilder.ToString();

			StringBuilder argsBuilder = new();
			foreach (UhtType parameter in function.ParameterProperties.Span)
			{
				if (parameter is not UhtProperty property)
				{
					continue;
				}
				if (property == selfProperty)
				{
					continue;
				}
				if (argsBuilder.Length > 0)
				{
					argsBuilder.Append(", ");
				}
				property.AppendFunctionThunkParameterArg(argsBuilder);
			}

			UhtProperty? returnProperty = function.ReturnProperty;
			List<string> lines = new();
			if (info.IsStatic)
			{
				lines.Add(BuildCallLine(info.Struct.SourceName + "::", info.OriginalName, argsBuilder.ToString(), returnProperty, null));
				return lines;
			}
			lines.Add(BuildCallLine(selfName + ".", info.OriginalName, argsBuilder.ToString(), returnProperty, null));
			return lines;
		}

		private static string BuildCallLine(string callTargetPrefix, string functionName, string args, UhtProperty? returnProperty, string? indent)
		{
			string prefix = indent ?? "\t";
			string callExpr = string.IsNullOrEmpty(args)
				? $"{callTargetPrefix}{functionName}()"
				: $"{callTargetPrefix}{functionName}({args})";
			if (returnProperty != null)
			{
				StringBuilder returnTypeBuilder = new();
				returnTypeBuilder.AppendFunctionThunkReturn(returnProperty);
				return prefix + "*(" + returnTypeBuilder + "*)Z_Param__Result=" + callExpr + ';';
			}
			return prefix + callExpr + ';';
		}


		private static UhtProperty? FindSelfProperty(UhtFunction function)
		{
			foreach (UhtType parameter in function.ParameterProperties.Span)
			{
				if (parameter is UhtProperty property && property.MetaData.ContainsKey(StructFunctionConstants.StructFunctionSelfKey))
				{
					return property;
				}
			}
			return null;
		}



		private void ModifyGeneratedHeaderFile(UhtHeaderFile headerFile)
		{
			StructFunctionLibraryInfo? libraryInfo = null;
			foreach (StructFunctionInfo info in StructFunctionRegistry.Functions)
			{
				if (info.LibraryInfo.Class.HeaderFile == headerFile)
				{
					libraryInfo = info.LibraryInfo;
					break;
				}
			}
			if (libraryInfo == null)
			{
				return;
			}

			string headerPath = Path.Combine(headerFile.Module.Module.OutputDirectory, headerFile.FileNameWithoutExtension) + ".generated.h";
			if (!File.Exists(headerPath))
			{
				return;
			}

			string[] allLines = File.ReadAllLines(headerPath);
			string classDeclSignature = $"class {headerFile.Module.Api}{libraryInfo.ClassName} :";
			foreach (string line in allLines)
			{
				if (line.Contains(classDeclSignature, StringComparison.Ordinal))
				{
					return;
				}
			}

			string fileId = String.Empty;
			int insertIndex = -1;
			for (int i = 0; i < allLines.Length; i++)
			{
				if (allLines[i].StartsWith("#define CURRENT_FILE_ID ", StringComparison.Ordinal))
				{
					fileId = allLines[i].Substring("#define CURRENT_FILE_ID ".Length).Trim();
					insertIndex = i;
					break;
				}
			}
			if (String.IsNullOrEmpty(fileId) || insertIndex < 0)
			{
				return;
			}

			string generatedBodyMacro = $"{fileId}_{libraryInfo.GeneratedBodyLineNumber}_GENERATED_BODY";
			string[] classDeclLines = new string[]
			{
				$"class {headerFile.Module.Api}{libraryInfo.ClassName} : public UObject",
				"{",
				"public:",
				$"\t{generatedBodyMacro}",
				"};",
			};

			List<string> updatedLines = new(allLines.Length + classDeclLines.Length + 2);
			for (int i = 0; i <= insertIndex; i++)
			{
				updatedLines.Add(allLines[i]);
			}
			updatedLines.Add(String.Empty);
			updatedLines.AddRange(classDeclLines);
			for (int i = insertIndex + 1; i < allLines.Length; i++)
			{
				updatedLines.Add(allLines[i]);
			}

			File.WriteAllLines(headerPath, updatedLines);
		}

		private IUhtExportFactory Factory;
	}
}
