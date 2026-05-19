#include "Buildings/UOsm2MapBuildingGenerator.h"
#include "Terrain/UOsm2MapTerrainGenerator.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogBuildingGen, Log, All);

void UOsm2MapBuildingGenerator::Generate(
	UWorld* World,
	const TArray<FBuildingFootprint>& Footprints,
	const FOsmCoordinateConverter& CoordConverter,
	UOsm2MapTerrainGenerator* TerrainGen)
{
	int32 Generated = 0;
	for (const FBuildingFootprint& Footprint : Footprints)
	{
		if (Footprint.OuterRing.Num() < 3) continue;

		AActor* Building = GenerateBuilding(World, Footprint, CoordConverter, TerrainGen);
		if (Building)
		{
			Generated++;
		}
	}

	UE_LOG(LogBuildingGen, Log, TEXT("Generated %d/%d buildings"), Generated, Footprints.Num());
}

AActor* UOsm2MapBuildingGenerator::GenerateBuilding(
	UWorld* World,
	const FBuildingFootprint& Footprint,
	const FOsmCoordinateConverter& CoordConverter,
	UOsm2MapTerrainGenerator* TerrainGen)
{
	// Determine base Z from terrain
	float BaseZ = 0.0f;
	if (TerrainGen)
	{
		// Use the centroid of the footprint
		FVector2D Centroid(0, 0);
		for (const FVector2D& P : Footprint.OuterRing)
		{
			Centroid += P;
		}
		Centroid /= Footprint.OuterRing.Num();
		BaseZ = TerrainGen->GetElevationAtOsmXY(Centroid.X, Centroid.Y);
	}

	float HeightM = Footprint.Height;

	// Spawn actor
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(*FString::Printf(TEXT("Building_%lld"), Footprint.OsmId));
	AActor* BuildingActor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!BuildingActor)
	{
		return nullptr;
	}

	USceneComponent* Root = NewObject<USceneComponent>(BuildingActor, TEXT("Root"));
	BuildingActor->SetRootComponent(Root);
	Root->RegisterComponent();
	BuildingActor->AddInstanceComponent(Root);

	UProceduralMeshComponent* MeshComp = NewObject<UProceduralMeshComponent>(BuildingActor, TEXT("BuildingMesh"));
	MeshComp->SetupAttachment(Root);
	MeshComp->RegisterComponent();
	BuildingActor->AddInstanceComponent(MeshComp);

	if (Footprint.bIsComplex)
	{
		// Complex building: each wall face as a separate mesh section
		GenerateWallsSeparateSections(MeshComp, Footprint.OuterRing, BaseZ, HeightM, CoordConverter);

		// Roof as another section
		int32 RoofSection = Footprint.OuterRing.Num(); // After all wall sections
		TArray<FVector> RoofVerts, RoofNormals;
		TArray<int32> RoofTris;
		TArray<FVector2D> RoofUVs;
		GenerateCap(Footprint.OuterRing, BaseZ + HeightM, false, CoordConverter,
			RoofVerts, RoofTris, RoofNormals, RoofUVs);
		if (RoofVerts.Num() > 0)
		{
			MeshComp->CreateMeshSection(RoofSection, RoofVerts, RoofTris, RoofNormals, RoofUVs, TArray<FColor>(), TArray<FProcMeshTangent>(), true);
		}
	}
	else
	{
		// Simple building: walls + floor + roof in sections 0, 1, 2
		// Section 0: Walls
		TArray<FVector> WallVerts, WallNormals;
		TArray<int32> WallTris;
		TArray<FVector2D> WallUVs;
		GenerateWalls(Footprint.OuterRing, BaseZ, HeightM, CoordConverter,
			WallVerts, WallTris, WallNormals, WallUVs, 0, false);
		if (WallVerts.Num() > 0)
		{
			MeshComp->CreateMeshSection(0, WallVerts, WallTris, WallNormals, WallUVs, TArray<FColor>(), TArray<FProcMeshTangent>(), true);
		}

		// Section 1: Floor
		TArray<FVector> FloorVerts, FloorNormals;
		TArray<int32> FloorTris;
		TArray<FVector2D> FloorUVs;
		GenerateCap(Footprint.OuterRing, BaseZ, true, CoordConverter,
			FloorVerts, FloorTris, FloorNormals, FloorUVs);
		if (FloorVerts.Num() > 0)
		{
			MeshComp->CreateMeshSection(1, FloorVerts, FloorTris, FloorNormals, FloorUVs, TArray<FColor>(), TArray<FProcMeshTangent>(), true);
		}

		// Section 2: Roof
		TArray<FVector> RoofVerts, RoofNormals;
		TArray<int32> RoofTris;
		TArray<FVector2D> RoofUVs;
		GenerateCap(Footprint.OuterRing, BaseZ + HeightM, false, CoordConverter,
			RoofVerts, RoofTris, RoofNormals, RoofUVs);
		if (RoofVerts.Num() > 0)
		{
			MeshComp->CreateMeshSection(2, RoofVerts, RoofTris, RoofNormals, RoofUVs, TArray<FColor>(), TArray<FProcMeshTangent>(), true);
		}
	}

	BuildingActor->Tags.Add(FName("Osm2Map_Building"));

#if WITH_EDITOR
	BuildingActor->SetActorLabel(FString::Printf(TEXT("Building_%s_%lld"), *Footprint.BuildingType, Footprint.OsmId));
#endif

	return BuildingActor;
}

void UOsm2MapBuildingGenerator::GenerateWalls(
	const TArray<FVector2D>& Ring,
	float BaseZ, float Height,
	const FOsmCoordinateConverter& CoordConverter,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals,
	TArray<FVector2D>& OutUVs,
	int32 MeshSection,
	bool bSeparateSections)
{
	int32 N = Ring.Num();
	float CumulativeU = 0.0f;

	for (int32 i = 0; i < N; ++i)
	{
		int32 Next = (i + 1) % N;

		FVector BL = CoordConverter.OsmToUE(Ring[i].X, Ring[i].Y, BaseZ);
		FVector BR = CoordConverter.OsmToUE(Ring[Next].X, Ring[Next].Y, BaseZ);
		FVector TL = CoordConverter.OsmToUE(Ring[i].X, Ring[i].Y, BaseZ + Height);
		FVector TR = CoordConverter.OsmToUE(Ring[Next].X, Ring[Next].Y, BaseZ + Height);

		// Outward normal
		FVector Edge = BR - BL;
		FVector Up = TL - BL;
		FVector Normal = FVector::CrossProduct(Up, Edge).GetSafeNormal();

		float EdgeLen = Edge.Size();
		float UEnd = CumulativeU + EdgeLen;

		int32 BaseIdx = OutVertices.Num();

		OutVertices.Add(BL); OutNormals.Add(Normal); OutUVs.Add(FVector2D(CumulativeU / 100.0f, 0.0f));
		OutVertices.Add(BR); OutNormals.Add(Normal); OutUVs.Add(FVector2D(UEnd / 100.0f, 0.0f));
		OutVertices.Add(TL); OutNormals.Add(Normal); OutUVs.Add(FVector2D(CumulativeU / 100.0f, 1.0f));
		OutVertices.Add(TR); OutNormals.Add(Normal); OutUVs.Add(FVector2D(UEnd / 100.0f, 1.0f));

		// Two triangles for the quad (CCW winding = front face from exterior)
		OutTriangles.Add(BaseIdx + 0);
		OutTriangles.Add(BaseIdx + 1);
		OutTriangles.Add(BaseIdx + 2);

		OutTriangles.Add(BaseIdx + 1);
		OutTriangles.Add(BaseIdx + 3);
		OutTriangles.Add(BaseIdx + 2);

		CumulativeU = UEnd;
	}
}

void UOsm2MapBuildingGenerator::GenerateWallsSeparateSections(
	UProceduralMeshComponent* MeshComp,
	const TArray<FVector2D>& Ring,
	float BaseZ, float Height,
	const FOsmCoordinateConverter& CoordConverter)
{
	int32 N = Ring.Num();

	for (int32 i = 0; i < N; ++i)
	{
		int32 Next = (i + 1) % N;

		FVector BL = CoordConverter.OsmToUE(Ring[i].X, Ring[i].Y, BaseZ);
		FVector BR = CoordConverter.OsmToUE(Ring[Next].X, Ring[Next].Y, BaseZ);
		FVector TL = CoordConverter.OsmToUE(Ring[i].X, Ring[i].Y, BaseZ + Height);
		FVector TR = CoordConverter.OsmToUE(Ring[Next].X, Ring[Next].Y, BaseZ + Height);

		FVector Edge = BR - BL;
		FVector Up = TL - BL;
		FVector Normal = FVector::CrossProduct(Up, Edge).GetSafeNormal();

		float EdgeLen = Edge.Size();

		TArray<FVector> Verts = { BL, BR, TL, TR };
		TArray<FVector> Normals = { Normal, Normal, Normal, Normal };
		TArray<FVector2D> UVs = {
			FVector2D(0.0f, 0.0f),
			FVector2D(EdgeLen / 100.0f, 0.0f),
			FVector2D(0.0f, 1.0f),
			FVector2D(EdgeLen / 100.0f, 1.0f)
		};
		TArray<int32> Tris = { 0, 1, 2, 1, 3, 2 };

		MeshComp->CreateMeshSection(i, Verts, Tris, Normals, UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), true);
	}
}

void UOsm2MapBuildingGenerator::GenerateCap(
	const TArray<FVector2D>& Ring,
	float Z,
	bool bFlipNormals,
	const FOsmCoordinateConverter& CoordConverter,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals,
	TArray<FVector2D>& OutUVs)
{
	TArray<int32> Indices = TriangulatePolygon(Ring);
	if (Indices.Num() == 0) return;

	FVector Normal = bFlipNormals ? FVector(0, 0, -1) : FVector(0, 0, 1);

	// Find bounding box for UV mapping
	FVector2D MinBound(TNumericLimits<double>::Max());
	FVector2D MaxBound(TNumericLimits<double>::Lowest());
	for (const FVector2D& P : Ring)
	{
		MinBound.X = FMath::Min(MinBound.X, P.X);
		MinBound.Y = FMath::Min(MinBound.Y, P.Y);
		MaxBound.X = FMath::Max(MaxBound.X, P.X);
		MaxBound.Y = FMath::Max(MaxBound.Y, P.Y);
	}
	FVector2D BoundSize = MaxBound - MinBound;
	if (BoundSize.X < 0.01) BoundSize.X = 1.0;
	if (BoundSize.Y < 0.01) BoundSize.Y = 1.0;

	for (const FVector2D& P : Ring)
	{
		OutVertices.Add(CoordConverter.OsmToUE(P.X, P.Y, Z));
		OutNormals.Add(Normal);
		OutUVs.Add(FVector2D((P.X - MinBound.X) / BoundSize.X, (P.Y - MinBound.Y) / BoundSize.Y));
	}

	if (bFlipNormals)
	{
		// Reverse triangle winding
		for (int32 i = 0; i < Indices.Num(); i += 3)
		{
			OutTriangles.Add(Indices[i]);
			OutTriangles.Add(Indices[i + 2]);
			OutTriangles.Add(Indices[i + 1]);
		}
	}
	else
	{
		OutTriangles = Indices;
	}
}

TArray<int32> UOsm2MapBuildingGenerator::TriangulatePolygon(const TArray<FVector2D>& Polygon)
{
	TArray<int32> Result;
	if (Polygon.Num() < 3) return Result;

	// Ear clipping triangulation
	TArray<int32> Remaining;
	Remaining.Reserve(Polygon.Num());
	for (int32 i = 0; i < Polygon.Num(); ++i)
	{
		Remaining.Add(i);
	}

	int32 FailCount = 0;
	while (Remaining.Num() > 2 && FailCount < Remaining.Num())
	{
		bool bFoundEar = false;
		for (int32 i = 0; i < Remaining.Num(); ++i)
		{
			int32 PrevIdx = (i + Remaining.Num() - 1) % Remaining.Num();
			int32 NextIdx = (i + 1) % Remaining.Num();

			int32 Prev = Remaining[PrevIdx];
			int32 Curr = Remaining[i];
			int32 Next = Remaining[NextIdx];

			// Check if this is a convex vertex (ear candidate)
			const FVector2D& A = Polygon[Prev];
			const FVector2D& B = Polygon[Curr];
			const FVector2D& C = Polygon[Next];

			double Cross = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
			if (Cross <= 0) continue; // Reflex vertex, not an ear

			// Check no other vertex inside this triangle
			bool bContainsOther = false;
			for (int32 j = 0; j < Remaining.Num(); ++j)
			{
				if (j == PrevIdx || j == i || j == NextIdx) continue;
				if (IsPointInTriangle(Polygon[Remaining[j]], A, B, C))
				{
					bContainsOther = true;
					break;
				}
			}

			if (!bContainsOther)
			{
				Result.Add(Prev);
				Result.Add(Curr);
				Result.Add(Next);
				Remaining.RemoveAt(i);
				bFoundEar = true;
				FailCount = 0;
				break;
			}
		}

		if (!bFoundEar)
		{
			FailCount++;
		}
	}

	return Result;
}

bool UOsm2MapBuildingGenerator::IsPointInTriangle(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
{
	double D1 = (P.X - C.X) * (A.Y - C.Y) - (A.X - C.X) * (P.Y - C.Y);
	double D2 = (P.X - A.X) * (B.Y - A.Y) - (B.X - A.X) * (P.Y - A.Y);
	double D3 = (P.X - B.X) * (C.Y - B.Y) - (C.X - B.X) * (P.Y - B.Y);

	bool bHasNeg = (D1 < 0) || (D2 < 0) || (D3 < 0);
	bool bHasPos = (D1 > 0) || (D2 > 0) || (D3 > 0);

	return !(bHasNeg && bHasPos);
}

bool UOsm2MapBuildingGenerator::IsEar(const TArray<FVector2D>& Polygon, int32 Prev, int32 Curr, int32 Next)
{
	const FVector2D& A = Polygon[Prev];
	const FVector2D& B = Polygon[Curr];
	const FVector2D& C = Polygon[Next];

	// Must be convex
	double Cross = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
	if (Cross <= 0) return false;

	// No other point inside
	for (int32 i = 0; i < Polygon.Num(); ++i)
	{
		if (i == Prev || i == Curr || i == Next) continue;
		if (IsPointInTriangle(Polygon[i], A, B, C)) return false;
	}

	return true;
}
