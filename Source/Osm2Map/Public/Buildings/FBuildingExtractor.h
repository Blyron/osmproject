#pragma once

#include "CoreMinimal.h"
#include "Pipeline/FOsmRawData.h"
#include "Pipeline/FOsmCategorizer.h"

/**
 * Extracted building footprint data ready for mesh generation.
 */
struct OSM2MAP_API FBuildingFootprint
{
	/** Outer boundary polygon in osm2xodr local meters (closed, first == last) */
	TArray<FVector2D> OuterRing;

	/** Inner rings (courtyards/holes) in osm2xodr local meters */
	TArray<TArray<FVector2D>> Holes;

	/** Building height in meters */
	float Height = 9.0f;

	/** Building type from OSM tag (house, apartments, commercial, etc.) */
	FString BuildingType;

	/** Roof shape (flat, gabled, hipped) */
	FString RoofShape = TEXT("flat");

	/** All OSM tags for this building */
	TMap<FString, FString> Tags;

	/** Original OSM way ID */
	int64 OsmId = 0;

	/** Whether this is a complex building (concave or has holes) */
	bool bIsComplex = false;
};

/**
 * Extracts building footprints from OSM data, resolving multipolygon relations.
 */
class OSM2MAP_API FBuildingExtractor
{
public:
	static TArray<FBuildingFootprint> Extract(
		const FOsmRawData& RawData,
		const FOsmCategories& Categories,
		const TMap<int64, FVector2D>& ProjectedNodes,
		float DefaultHeight = 9.0f);

private:
	/** Extract a simple building from a single way */
	static FBuildingFootprint ExtractFromWay(
		const FOsmWayData& Way,
		const TMap<int64, FVector2D>& ProjectedNodes,
		float DefaultHeight);

	/** Extract complex buildings from multipolygon relations */
	static TArray<FBuildingFootprint> ExtractFromRelation(
		const FOsmRelationData& Relation,
		const FOsmRawData& RawData,
		const TMap<int64, FVector2D>& ProjectedNodes,
		float DefaultHeight);

	/** Determine height from OSM tags */
	static float DetermineHeight(const TMap<FString, FString>& Tags, float DefaultHeight);

	/** Check if a polygon is convex */
	static bool IsConvex(const TArray<FVector2D>& Polygon);

	/** Ensure polygon has counter-clockwise winding */
	static void EnsureCCW(TArray<FVector2D>& Polygon);
};
