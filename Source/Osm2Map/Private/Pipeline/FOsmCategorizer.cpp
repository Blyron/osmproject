#include "Pipeline/FOsmCategorizer.h"

static const TSet<FString> DrivableHighwayTypes = {
	TEXT("motorway"), TEXT("motorway_link"),
	TEXT("trunk"), TEXT("trunk_link"),
	TEXT("primary"), TEXT("primary_link"),
	TEXT("secondary"), TEXT("secondary_link"),
	TEXT("tertiary"), TEXT("tertiary_link"),
	TEXT("residential"),
	TEXT("living_street"),
	TEXT("service"),
	TEXT("unclassified")
};

static const TSet<FString> NaturalAreaTags = {
	TEXT("wood"), TEXT("forest"), TEXT("grass"),
	TEXT("scrub"), TEXT("heath"), TEXT("meadow")
};

static const TSet<FString> LanduseParkTags = {
	TEXT("park"), TEXT("forest"), TEXT("grass"),
	TEXT("recreation_ground"), TEXT("village_green"),
	TEXT("allotments"), TEXT("orchard")
};

static const TSet<FString> WaterwayTags = {
	TEXT("river"), TEXT("stream"), TEXT("canal"),
	TEXT("drain"), TEXT("ditch")
};

static const TSet<FString> PlaceableAmenityKeys = {
	TEXT("amenity"), TEXT("man_made"), TEXT("tourism"),
	TEXT("historic"), TEXT("emergency"), TEXT("barrier"),
	TEXT("information")
};

FOsmCategories FOsmCategorizer::Categorize(const FOsmRawData& Data)
{
	FOsmCategories Result;

	// Categorize ways
	for (const auto& [Id, Way] : Data.Ways)
	{
		if (IsDrivableHighway(Way.GetTag(TEXT("highway"))))
		{
			Result.RoadWayIds.Add(Id);
		}

		if (IsBuildingWay(Way))
		{
			Result.BuildingWayIds.Add(Id);
		}

		if (IsNaturalArea(Way))
		{
			Result.NaturalAreaIds.Add(Id);
		}

		if (IsWaterway(Way))
		{
			Result.WaterwayIds.Add(Id);
		}
	}

	// Categorize nodes (amenities, trees, lamps, etc.)
	for (const auto& [Id, Node] : Data.Nodes)
	{
		if (IsPlaceableAmenity(Node))
		{
			Result.AmenityNodeIds.Add(Id);
		}
	}

	// Categorize relations (multipolygons for complex buildings, areas)
	for (const auto& [Id, Rel] : Data.Relations)
	{
		const FString* TypeTag = Rel.Tags.Find(TEXT("type"));
		if (TypeTag && *TypeTag == TEXT("multipolygon"))
		{
			Result.MultipolygonRelationIds.Add(Id);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("OSM Categorized: %d roads, %d buildings, %d natural areas, %d waterways, %d amenities, %d multipolygons"),
		Result.RoadWayIds.Num(), Result.BuildingWayIds.Num(), Result.NaturalAreaIds.Num(),
		Result.WaterwayIds.Num(), Result.AmenityNodeIds.Num(), Result.MultipolygonRelationIds.Num());

	return Result;
}

bool FOsmCategorizer::IsDrivableHighway(const FString& HighwayType)
{
	return !HighwayType.IsEmpty() && DrivableHighwayTypes.Contains(HighwayType);
}

bool FOsmCategorizer::IsBuildingWay(const FOsmWayData& Way)
{
	return Way.HasTag(TEXT("building"));
}

bool FOsmCategorizer::IsNaturalArea(const FOsmWayData& Way)
{
	const FString Natural = Way.GetTag(TEXT("natural"));
	if (!Natural.IsEmpty() && NaturalAreaTags.Contains(Natural))
	{
		return true;
	}

	const FString Landuse = Way.GetTag(TEXT("landuse"));
	if (!Landuse.IsEmpty() && LanduseParkTags.Contains(Landuse))
	{
		return true;
	}

	const FString Leisure = Way.GetTag(TEXT("leisure"));
	if (Leisure == TEXT("park") || Leisure == TEXT("garden") || Leisure == TEXT("nature_reserve"))
	{
		return true;
	}

	return false;
}

bool FOsmCategorizer::IsWaterway(const FOsmWayData& Way)
{
	const FString Waterway = Way.GetTag(TEXT("waterway"));
	if (!Waterway.IsEmpty() && WaterwayTags.Contains(Waterway))
	{
		return true;
	}

	const FString Natural = Way.GetTag(TEXT("natural"));
	return Natural == TEXT("water");
}

bool FOsmCategorizer::IsPlaceableAmenity(const FOsmNodeData& Node)
{
	if (Node.Tags.Num() == 0)
	{
		return false;
	}

	// Trees
	const FString* Natural = Node.Tags.Find(TEXT("natural"));
	if (Natural && *Natural == TEXT("tree"))
	{
		return true;
	}

	// Street lamps
	const FString* Highway = Node.Tags.Find(TEXT("highway"));
	if (Highway && (*Highway == TEXT("street_lamp") || *Highway == TEXT("traffic_signals") || *Highway == TEXT("bus_stop")))
	{
		return true;
	}

	// Check amenity keys
	for (const FString& Key : PlaceableAmenityKeys)
	{
		if (Node.Tags.Contains(Key))
		{
			return true;
		}
	}

	return false;
}
