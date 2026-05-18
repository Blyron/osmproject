#pragma once

#include "CoreMinimal.h"
#include "FOsmRawData.generated.h"

// Forward declare osm2xodr types
struct OsmData;

USTRUCT(BlueprintType)
struct OSM2MAP_API FOsmNodeData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	int64 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	double Lat = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	double Lon = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	TMap<FString, FString> Tags;
};

USTRUCT(BlueprintType)
struct OSM2MAP_API FOsmWayData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	int64 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	TArray<int64> NodeRefs;

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	TMap<FString, FString> Tags;

	FString GetTag(const FString& Key) const
	{
		const FString* Value = Tags.Find(Key);
		return Value ? *Value : FString();
	}

	bool HasTag(const FString& Key) const
	{
		return Tags.Contains(Key);
	}
};

USTRUCT(BlueprintType)
struct OSM2MAP_API FOsmRelationMember
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	FString Type;

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	int64 Ref = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	FString Role;
};

USTRUCT(BlueprintType)
struct OSM2MAP_API FOsmRelationData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	int64 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	TArray<FOsmRelationMember> Members;

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	TMap<FString, FString> Tags;
};

USTRUCT(BlueprintType)
struct OSM2MAP_API FOsmRawData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	TMap<int64, FOsmNodeData> Nodes;

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	TMap<int64, FOsmWayData> Ways;

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	TMap<int64, FOsmRelationData> Relations;

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	double MinLat = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	double MaxLat = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	double MinLon = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "OSM")
	double MaxLon = 0.0;

	/** Convert from osm2xodr native OsmData to UE-friendly FOsmRawData */
	static FOsmRawData FromOsmData(const OsmData& Data);

	/** Convert back to osm2xodr native OsmData (for passing to RoadNetwork::build, etc.) */
	OsmData ToOsmData() const;

	bool IsValid() const { return Nodes.Num() > 0; }
};
