#pragma once

#include "CoreMinimal.h"
#include "Buildings/FBuildingExtractor.h"
#include "Coord/FOsmCoordinateConverter.h"
#include "UOsm2MapBuildingGenerator.generated.h"

class UWorld;
class UProceduralMeshComponent;
class UOsm2MapTerrainGenerator;

/**
 * Generates 3D building meshes from extracted footprints.
 * - Simple buildings: single mesh with walls, floor, and roof
 * - Complex buildings: each wall face as a separate mesh section
 */
UCLASS(BlueprintType)
class OSM2MAP_API UOsm2MapBuildingGenerator : public UObject
{
	GENERATED_BODY()

public:
	/** Generate all buildings in the world */
	void Generate(
		UWorld* World,
		const TArray<FBuildingFootprint>& Footprints,
		const FOsmCoordinateConverter& CoordConverter,
		UOsm2MapTerrainGenerator* TerrainGen = nullptr
	);

private:
	/** Generate mesh for a single building */
	AActor* GenerateBuilding(
		UWorld* World,
		const FBuildingFootprint& Footprint,
		const FOsmCoordinateConverter& CoordConverter,
		UOsm2MapTerrainGenerator* TerrainGen
	);

	/** Generate wall geometry for all edges */
	void GenerateWalls(
		const TArray<FVector2D>& Ring,
		float BaseZ, float Height,
		const FOsmCoordinateConverter& CoordConverter,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals,
		TArray<FVector2D>& OutUVs,
		int32 MeshSection,
		bool bSeparateSections
	);

	/** Generate wall geometry for complex buildings (each face = separate section) */
	void GenerateWallsSeparateSections(
		UProceduralMeshComponent* MeshComp,
		const TArray<FVector2D>& Ring,
		float BaseZ, float Height,
		const FOsmCoordinateConverter& CoordConverter
	);

	/** Generate floor or roof cap */
	void GenerateCap(
		const TArray<FVector2D>& Ring,
		float Z,
		bool bFlipNormals,
		const FOsmCoordinateConverter& CoordConverter,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals,
		TArray<FVector2D>& OutUVs
	);

	/** Simple ear-clipping triangulation for 2D polygon */
	static TArray<int32> TriangulatePolygon(const TArray<FVector2D>& Polygon);

	/** Check if a point is inside a triangle */
	static bool IsPointInTriangle(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C);

	/** Check if an ear is valid (no other vertices inside) */
	static bool IsEar(const TArray<FVector2D>& Polygon, int32 Prev, int32 Curr, int32 Next);
};
