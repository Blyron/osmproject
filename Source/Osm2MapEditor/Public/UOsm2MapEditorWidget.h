#pragma once

#include "CoreMinimal.h"
#include "EditorUtilityWidget.h"
#include "Pipeline/FOsmImportSettings.h"
#include "UOsm2MapEditorWidget.generated.h"

class UOsmAssetDictionary;

UCLASS(Blueprintable)
class OSM2MAPEDITOR_API UOsm2MapEditorWidget : public UEditorUtilityWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Osm2Map")
	bool RunImport(const FOsmImportSettings& Settings);

	UFUNCTION(BlueprintCallable, Category = "Osm2Map|Asset Dictionary")
	bool ExportDictionaryToJson(UOsmAssetDictionary* Dictionary, const FString& FilePath);

	UFUNCTION(BlueprintCallable, Category = "Osm2Map|Asset Dictionary")
	bool ImportDictionaryFromJson(UOsmAssetDictionary* Dictionary, const FString& FilePath);
};