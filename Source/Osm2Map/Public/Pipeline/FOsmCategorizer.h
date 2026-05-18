#pragma once

#include "CoreMinimal.h"
#include "Pipeline/FOsmRawData.h"

/**
 * Categorizes OSM data into functional groups without modifying the original dataset.
 * Unlike OsmFilter (which destructively removes non-road data), this creates index arrays
 * that reference into the full FOsmRawData.
 */
struct OSM2MAP_API FOsmCategories
{
	/** Ways with highway=motorway|trunk|primary|secondary|tertiary|residential|service|... */
	TArray<int64> RoadWayIds;

	/** Ways with building=* */
	TArray<int64> BuildingWayIds;

	/** Ways with natural=wood, landuse=park|forest|grass, leisure=park */
	TArray<int64> NaturalAreaIds;

	/** Ways with waterway=river|stream|canal, natural=water */
	TArray<int64> WaterwayIds;

	/** Nodes with amenity=*, natural=tree, highway=street_lamp, man_made=*, etc. */
	TArray<int64> AmenityNodeIds;

	/** Relations with type=multipolygon (complex buildings, areas) */
	TArray<int64> MultipolygonRelationIds;
};

class OSM2MAP_API FOsmCategorizer
{
public:
	static FOsmCategories Categorize(const FOsmRawData& Data);

private:
	static bool IsDrivableHighway(const FString& HighwayType);
	static bool IsBuildingWay(const FOsmWayData& Way);
	static bool IsNaturalArea(const FOsmWayData& Way);
	static bool IsWaterway(const FOsmWayData& Way);
	static bool IsPlaceableAmenity(const FOsmNodeData& Node);
};
