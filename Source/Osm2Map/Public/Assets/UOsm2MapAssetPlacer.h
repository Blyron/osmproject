#pragma once

#include "CoreMinimal.h"
#include "Pipeline/FOsmRawData.h"
#include "Pipeline/FOsmCategorizer.h"
#include "Coord/FOsmCoordinateConverter.h"
#include "Assets/UOsmAssetDictionary.h"
#include "UOsm2MapAssetPlacer.generated.h"

class UWorld;
class UOsm2MapTerrainGenerator;
class UHierarchicalInstancedStaticMeshComponent;

/**
 * Places assets (trees, lamps, signs, etc.) based on OSM tags and the asset dictionary.
 * Uses HISM for instanced rendering, foliage system for trees.
 */
UCLASS(BlueprintType)
class OSM2MAP_API UOsm2MapAssetPlacer : public UObject
{
	GENERATED_BODY()

public:
	/** Place all matching assets from OSM data */
	void PlaceAssets(
		UWorld* World,
		const FOsmRawData& RawData,
		const FOsmCategories& Categories,
		const TMap<int64, FVector2D>& ProjectedNodes,
		const FOsmCoordinateConverter& CoordConverter,
		UOsmAssetDictionary* Dictionary,
		UOsm2MapTerrainGenerator* TerrainGen = nullptr
	);

	/** Place trees in forested/park areas using Poisson disk sampling */
	void PlaceAreaVegetation(
		UWorld* World,
		const FOsmRawData& RawData,
		const FOsmCategories& Categories,
		const TMap<int64, FVector2D>& ProjectedNodes,
		const FOsmCoordinateConverter& CoordConverter,
		UOsmAssetDictionary* Dictionary,
		UOsm2MapTerrainGenerator* TerrainGen = nullptr
	);

private:
	/** Get or create HISM component for a given mesh */
	UHierarchicalInstancedStaticMeshComponent* GetOrCreateHISM(
		AActor* Owner,
		UStaticMesh* Mesh,
		const FString& Name
	);

	/** Simple Poisson disk sampling within a bounding box */
	TArray<FVector2D> PoissonDiskSample(
		const FVector2D& Min, const FVector2D& Max,
		float MinDistance, int32 MaxAttempts = 30
	) const;

	/** Point in polygon test */
	static bool IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon);

	/** Cached HISM components by mesh path */
	TMap<FString, UHierarchicalInstancedStaticMeshComponent*> HISMCache;
};
