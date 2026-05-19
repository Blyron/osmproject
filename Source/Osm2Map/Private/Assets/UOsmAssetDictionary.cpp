#include "Assets/UOsmAssetDictionary.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsmAssetDictionary, Log, All);

namespace
{
	TSharedPtr<FJsonObject> SerializeRule(const FOsmAssetMappingRule& Rule)
	{
		TSharedPtr<FJsonObject> JsonRule = MakeShared<FJsonObject>();
		JsonRule->SetStringField(TEXT("tag_key"), Rule.TagKey);
		JsonRule->SetStringField(TEXT("tag_value"), Rule.TagValue);
		JsonRule->SetNumberField(TEXT("scale_min"), Rule.ScaleRange.X);
		JsonRule->SetNumberField(TEXT("scale_max"), Rule.ScaleRange.Y);
		JsonRule->SetNumberField(TEXT("yaw_min"), Rule.YawRange.X);
		JsonRule->SetNumberField(TEXT("yaw_max"), Rule.YawRange.Y);
		JsonRule->SetNumberField(TEXT("z_offset"), Rule.ZOffset);
		JsonRule->SetNumberField(TEXT("priority"), Rule.Priority);
		JsonRule->SetBoolField(TEXT("use_instancing"), Rule.bUseInstancing);
		JsonRule->SetBoolField(TEXT("use_foliage_system"), Rule.bUseFoliageSystem);

		TSharedPtr<FJsonObject> AdditionalTagsObject = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Pair : Rule.AdditionalTags)
		{
			AdditionalTagsObject->SetStringField(Pair.Key, Pair.Value);
		}
		JsonRule->SetObjectField(TEXT("additional_tags"), AdditionalTagsObject);

		TArray<TSharedPtr<FJsonValue>> MeshValues;
		for (const TSoftObjectPtr<UStaticMesh>& MeshVariant : Rule.MeshVariants)
		{
			MeshValues.Add(MakeShared<FJsonValueString>(MeshVariant.ToSoftObjectPath().ToString()));
		}
		JsonRule->SetArrayField(TEXT("mesh_variants"), MeshValues);

		return JsonRule;
	}

	bool DeserializeRule(const TSharedPtr<FJsonObject>& JsonRule, FOsmAssetMappingRule& OutRule)
	{
		if (!JsonRule.IsValid())
		{
			return false;
		}

		OutRule = FOsmAssetMappingRule();
		JsonRule->TryGetStringField(TEXT("tag_key"), OutRule.TagKey);
		JsonRule->TryGetStringField(TEXT("tag_value"), OutRule.TagValue);

		double NumberValue = 0.0;
		if (JsonRule->TryGetNumberField(TEXT("scale_min"), NumberValue))
		{
			OutRule.ScaleRange.X = static_cast<float>(NumberValue);
		}
		if (JsonRule->TryGetNumberField(TEXT("scale_max"), NumberValue))
		{
			OutRule.ScaleRange.Y = static_cast<float>(NumberValue);
		}
		if (JsonRule->TryGetNumberField(TEXT("yaw_min"), NumberValue))
		{
			OutRule.YawRange.X = static_cast<float>(NumberValue);
		}
		if (JsonRule->TryGetNumberField(TEXT("yaw_max"), NumberValue))
		{
			OutRule.YawRange.Y = static_cast<float>(NumberValue);
		}
		if (JsonRule->TryGetNumberField(TEXT("z_offset"), NumberValue))
		{
			OutRule.ZOffset = static_cast<float>(NumberValue);
		}

		int32 IntValue = 0;
		if (JsonRule->TryGetNumberField(TEXT("priority"), IntValue))
		{
			OutRule.Priority = IntValue;
		}

		JsonRule->TryGetBoolField(TEXT("use_instancing"), OutRule.bUseInstancing);
		JsonRule->TryGetBoolField(TEXT("use_foliage_system"), OutRule.bUseFoliageSystem);

		const TSharedPtr<FJsonObject>* AdditionalTagsObject = nullptr;
		if (JsonRule->TryGetObjectField(TEXT("additional_tags"), AdditionalTagsObject) && AdditionalTagsObject && AdditionalTagsObject->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*AdditionalTagsObject)->Values)
			{
				FString Value;
				if (Pair.Value.IsValid() && Pair.Value->TryGetString(Value))
				{
					OutRule.AdditionalTags.Add(Pair.Key, Value);
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* MeshValues = nullptr;
		if (JsonRule->TryGetArrayField(TEXT("mesh_variants"), MeshValues) && MeshValues)
		{
			for (const TSharedPtr<FJsonValue>& MeshValue : *MeshValues)
			{
				FString MeshPath;
				if (MeshValue.IsValid() && MeshValue->TryGetString(MeshPath) && !MeshPath.IsEmpty())
				{
					OutRule.MeshVariants.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(MeshPath)));
				}
			}
		}

		return !OutRule.TagKey.IsEmpty();
	}

	void AddDefaultRule(
		TArray<FOsmAssetMappingRule>& Rules,
		const TCHAR* TagKey,
		const TCHAR* TagValue,
		const FVector2D& ScaleRange,
		const FVector2D& YawRange,
		float ZOffset,
		int32 Priority,
		bool bUseInstancing,
		bool bUseFoliageSystem,
		std::initializer_list<TPair<FString, FString>> AdditionalTags = {})
	{
		FOsmAssetMappingRule& Rule = Rules.AddDefaulted_GetRef();
		Rule.TagKey = TagKey;
		Rule.TagValue = TagValue;
		Rule.ScaleRange = ScaleRange;
		Rule.YawRange = YawRange;
		Rule.ZOffset = ZOffset;
		Rule.Priority = Priority;
		Rule.bUseInstancing = bUseInstancing;
		Rule.bUseFoliageSystem = bUseFoliageSystem;
		for (const TPair<FString, FString>& Pair : AdditionalTags)
		{
			Rule.AdditionalTags.Add(Pair.Key, Pair.Value);
		}
	}
}

bool UOsmAssetDictionary::ExportToJson(const FString& FilePath) const
{
	if (FilePath.IsEmpty())
	{
		UE_LOG(LogOsmAssetDictionary, Error, TEXT("Export path is empty"));
		return false;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetNumberField(TEXT("version"), 1);

	TArray<TSharedPtr<FJsonValue>> JsonRules;
	JsonRules.Reserve(Rules.Num());
	for (const FOsmAssetMappingRule& Rule : Rules)
	{
		JsonRules.Add(MakeShared<FJsonValueObject>(SerializeRule(Rule)));
	}
	RootObject->SetArrayField(TEXT("rules"), JsonRules);

	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		UE_LOG(LogOsmAssetDictionary, Error, TEXT("Failed to serialize asset dictionary to JSON"));
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
	if (!FFileHelper::SaveStringToFile(OutputString, *FilePath))
	{
		UE_LOG(LogOsmAssetDictionary, Error, TEXT("Failed to write asset dictionary JSON: %s"), *FilePath);
		return false;
	}

	return true;
}

bool UOsmAssetDictionary::ImportFromJson(const FString& FilePath)
{
	if (FilePath.IsEmpty())
	{
		UE_LOG(LogOsmAssetDictionary, Error, TEXT("Import path is empty"));
		return false;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogOsmAssetDictionary, Error, TEXT("Failed to read asset dictionary JSON: %s"), *FilePath);
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		UE_LOG(LogOsmAssetDictionary, Error, TEXT("Failed to parse asset dictionary JSON: %s"), *FilePath);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonRules = nullptr;
	if (!RootObject->TryGetArrayField(TEXT("rules"), JsonRules) || !JsonRules)
	{
		UE_LOG(LogOsmAssetDictionary, Error, TEXT("Asset dictionary JSON is missing a rules array: %s"), *FilePath);
		return false;
	}

	TArray<FOsmAssetMappingRule> ImportedRules;
	ImportedRules.Reserve(JsonRules->Num());
	for (const TSharedPtr<FJsonValue>& JsonValue : *JsonRules)
	{
		const TSharedPtr<FJsonObject>* JsonRuleObject = nullptr;
		if (!JsonValue.IsValid() || !JsonValue->TryGetObject(JsonRuleObject) || !JsonRuleObject)
		{
			continue;
		}

		FOsmAssetMappingRule ImportedRule;
		if (DeserializeRule(*JsonRuleObject, ImportedRule))
		{
			ImportedRules.Add(MoveTemp(ImportedRule));
		}
	}

	Modify();
	Rules = MoveTemp(ImportedRules);
	MarkPackageDirty();
	return true;
}

int32 UOsmAssetDictionary::FindMatchingRule(const TMap<FString, FString>& Tags) const
{
	int32 BestIndex = INDEX_NONE;
	int32 BestPriority = TNumericLimits<int32>::Min();

	for (int32 i = 0; i < Rules.Num(); ++i)
	{
		const FOsmAssetMappingRule& Rule = Rules[i];

		if (Rule.Priority <= BestPriority && BestIndex != INDEX_NONE)
		{
			continue; // Already have a better match
		}

		// Check primary tag
		const FString* TagValue = Tags.Find(Rule.TagKey);
		if (!TagValue) continue;

		// If TagValue is specified, it must match
		if (!Rule.TagValue.IsEmpty() && *TagValue != Rule.TagValue)
		{
			continue;
		}

		// Check additional tags
		bool bAllAdditionalMatch = true;
		for (const auto& [Key, Value] : Rule.AdditionalTags)
		{
			const FString* ActualValue = Tags.Find(Key);
			if (!ActualValue || *ActualValue != Value)
			{
				bAllAdditionalMatch = false;
				break;
			}
		}
		if (!bAllAdditionalMatch) continue;

		// Check that the rule has at least one mesh variant
		if (Rule.MeshVariants.Num() == 0) continue;

		BestIndex = i;
		BestPriority = Rule.Priority;
	}

	return BestIndex;
}

void UOsmAssetDictionary::PopulateDefaults()
{
	Rules.Empty();

	AddDefaultRule(Rules, TEXT("natural"), TEXT("tree"), FVector2D(0.8f, 1.2f), FVector2D(0.0f, 360.0f), 0.0f, 10, true, true);
	AddDefaultRule(Rules, TEXT("natural"), TEXT("shrub"), FVector2D(0.7f, 1.1f), FVector2D(0.0f, 360.0f), 0.0f, 8, true, false);
	AddDefaultRule(Rules, TEXT("natural"), TEXT("bush"), FVector2D(0.7f, 1.1f), FVector2D(0.0f, 360.0f), 0.0f, 8, true, false);
	AddDefaultRule(Rules, TEXT("highway"), TEXT("street_lamp"), FVector2D(1.0f, 1.0f), FVector2D(0.0f, 360.0f), 0.0f, 10, true, false);
	AddDefaultRule(Rules, TEXT("highway"), TEXT("traffic_signals"), FVector2D(1.0f, 1.0f), FVector2D(0.0f, 0.0f), 0.0f, 10, false, false);
	AddDefaultRule(Rules, TEXT("highway"), TEXT("bus_stop"), FVector2D(1.0f, 1.0f), FVector2D(0.0f, 0.0f), 0.0f, 10, false, false);
	AddDefaultRule(Rules, TEXT("amenity"), TEXT("bench"), FVector2D(1.0f, 1.0f), FVector2D(0.0f, 0.0f), 0.0f, 10, true, false);
	AddDefaultRule(Rules, TEXT("amenity"), TEXT("waste_basket"), FVector2D(0.9f, 1.1f), FVector2D(0.0f, 360.0f), 0.0f, 10, true, false);
	AddDefaultRule(Rules, TEXT("amenity"), TEXT("recycling"), FVector2D(0.9f, 1.1f), FVector2D(0.0f, 360.0f), 0.0f, 10, true, false);
	AddDefaultRule(Rules, TEXT("amenity"), TEXT("waste_disposal"), FVector2D(1.0f, 1.1f), FVector2D(0.0f, 360.0f), 0.0f, 10, true, false);
	AddDefaultRule(Rules, TEXT("amenity"), TEXT("bicycle_parking"), FVector2D(1.0f, 1.0f), FVector2D(0.0f, 360.0f), 0.0f, 8, false, false);
	AddDefaultRule(Rules, TEXT("amenity"), TEXT("parking_meter"), FVector2D(1.0f, 1.0f), FVector2D(0.0f, 360.0f), 0.0f, 8, true, false);
	AddDefaultRule(Rules, TEXT("amenity"), TEXT("fountain"), FVector2D(1.0f, 1.2f), FVector2D(0.0f, 360.0f), 0.0f, 8, false, false);
	AddDefaultRule(Rules, TEXT("amenity"), TEXT("post_box"), FVector2D(1.0f, 1.0f), FVector2D(0.0f, 360.0f), 0.0f, 8, true, false);
	AddDefaultRule(Rules, TEXT("barrier"), TEXT("bollard"), FVector2D(1.0f, 1.0f), FVector2D(0.0f, 360.0f), 0.0f, 10, true, false);
	AddDefaultRule(Rules, TEXT("barrier"), TEXT("fence"), FVector2D(1.0f, 1.0f), FVector2D(0.0f, 360.0f), 0.0f, 6, false, false);
	AddDefaultRule(Rules, TEXT("barrier"), TEXT("wall"), FVector2D(1.0f, 1.0f), FVector2D(0.0f, 360.0f), 0.0f, 6, false, false);
	AddDefaultRule(Rules, TEXT("barrier"), TEXT("hedge"), FVector2D(1.0f, 1.2f), FVector2D(0.0f, 360.0f), 0.0f, 6, true, false);
	AddDefaultRule(Rules, TEXT("emergency"), TEXT("fire_hydrant"), FVector2D(1.0f, 1.0f), FVector2D(0.0f, 360.0f), 0.0f, 10, true, false);
	AddDefaultRule(Rules, TEXT("information"), TEXT("guidepost"), FVector2D(1.0f, 1.0f), FVector2D(0.0f, 360.0f), 0.0f, 5, true, false);
	AddDefaultRule(Rules, TEXT("shop"), TEXT("kiosk"), FVector2D(1.0f, 1.1f), FVector2D(0.0f, 360.0f), 0.0f, 4, false, false);
	AddDefaultRule(Rules, TEXT("amenity"), TEXT("cafe"), FVector2D(1.0f, 1.1f), FVector2D(0.0f, 360.0f), 0.0f, 4, false, false);
	AddDefaultRule(Rules, TEXT("amenity"), TEXT("restaurant"), FVector2D(1.0f, 1.1f), FVector2D(0.0f, 360.0f), 0.0f, 4, false, false);
	AddDefaultRule(Rules, TEXT("amenity"), TEXT("pub"), FVector2D(1.0f, 1.1f), FVector2D(0.0f, 360.0f), 0.0f, 4, false, false);
}
