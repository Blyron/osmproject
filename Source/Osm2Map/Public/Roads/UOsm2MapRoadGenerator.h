#pragma once

#include "CoreMinimal.h"
#include "Pipeline/FOsmRawData.h"
#include "Pipeline/FOsmCategorizer.h"
#include "Coord/FOsmCoordinateConverter.h"
#include "UOsm2MapRoadGenerator.generated.h"

class UWorld;
class UOsm2MapTerrainGenerator;
struct XodrNetwork;
struct XodrRoad;
struct XodrJunction;

/**
 * Generates 3D road meshes from OSM data using osm2xodr lane information.
 *
 * Pipeline:
 * 1. Filter OSM data to drivable roads only (copy)
 * 2. Build XodrNetwork via RoadNetwork::build()
 * 3. For each XodrRoad, generate ribbon mesh using lane widths
 * 4. Generate junction fill polygons
 * 5. Project onto terrain
 */
UCLASS(BlueprintType)
class OSM2MAP_API UOsm2MapRoadGenerator : public UObject
{
	GENERATED_BODY()

public:
	void Generate(
		UWorld* World,
		const FOsmRawData& RawData,
		const FOsmCategories& Categories,
		const TMap<int64, FVector2D>& ProjectedNodes,
		const FOsmCoordinateConverter& CoordConverter,
		float RoadZOffset,
		UOsm2MapTerrainGenerator* TerrainGen = nullptr
	);

private:
	/** Generate mesh for a single XodrRoad */
	AActor* GenerateRoadMesh(
		UWorld* World,
		const XodrRoad& Road,
		const FOsmCoordinateConverter& CoordConverter,
		float ZOffset,
		UOsm2MapTerrainGenerator* TerrainGen
	);

	/** Generate junction fill polygon mesh */
	AActor* GenerateJunctionMesh(
		UWorld* World,
		const XodrJunction& Junction,
		const XodrNetwork& Network,
		const FOsmCoordinateConverter& CoordConverter,
		float ZOffset,
		UOsm2MapTerrainGenerator* TerrainGen
	);

	/** Sample a point along a road's reference line */
	struct FRoadSample
	{
		FVector2D Position;  // osm local meters
		double Heading;      // radians
		double S;            // arc length
	};

	/** Sample the reference line at regular intervals */
	TArray<FRoadSample> SampleReferenceLine(const XodrRoad& Road, double StepSize = 2.0) const;

	/** Compute lane edge offsets at a given s-position for a road */
	struct FLaneEdge
	{
		double Offset;       // meters from center
		FString LaneType;    // "driving", "sidewalk", etc.
	};

	void ComputeLaneEdges(const XodrRoad& Road, double S,
		TArray<FLaneEdge>& OutLeftEdges, TArray<FLaneEdge>& OutRightEdges) const;
};
