#include "Assets/UOsm2MapAssetPlacer.h"
#include "Terrain/UOsm2MapTerrainGenerator.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogAssetPlacer, Log, All);

void UOsm2MapAssetPlacer::PlaceAssets(
	UWorld* World,
	const FOsmRawData& RawData,
	const FOsmCategories& Categories,
	const TMap<int64, FVector2D>& ProjectedNodes,
	const FOsmCoordinateConverter& CoordConverter,
	UOsmAssetDictionary* Dictionary,
	UOsm2MapTerrainGenerator* TerrainGen)
{
	if (!Dictionary)
	{
		UE_LOG(LogAssetPlacer, Warning, TEXT("No asset dictionary provided, skipping asset placement"));
		return;
	}

	// Create a container actor for instanced assets
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(TEXT("OsmAssets"));
	AActor* AssetContainer = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!AssetContainer)
	{
		UE_LOG(LogAssetPlacer, Error, TEXT("Failed to spawn asset container actor"));
		return;
	}

	USceneComponent* Root = NewObject<USceneComponent>(AssetContainer, TEXT("Root"));
	AssetContainer->SetRootComponent(Root);
	Root->RegisterComponent();

	int32 PlacedCount = 0;

	for (int64 NodeId : Categories.AmenityNodeIds)
	{
		const FOsmNodeData* Node = RawData.Nodes.Find(NodeId);
		if (!Node) continue;

		const FVector2D* OsmPos = ProjectedNodes.Find(NodeId);
		if (!OsmPos) continue;

		// Find matching rule
		int32 RuleIdx = Dictionary->FindMatchingRule(Node->Tags);
		if (RuleIdx == INDEX_NONE) continue;

		const FOsmAssetMappingRule& Rule = Dictionary->Rules[RuleIdx];

		// Select mesh variant deterministically based on OSM ID
		FRandomStream RandStream(static_cast<int32>(NodeId));
		int32 VariantIdx = RandStream.RandRange(0, Rule.MeshVariants.Num() - 1);

		UStaticMesh* Mesh = Rule.MeshVariants[VariantIdx].LoadSynchronous();
		if (!Mesh) continue;

		// Compute transform
		float ElevM = 0.0f;
		if (TerrainGen)
		{
			ElevM = TerrainGen->GetElevationAtOsmXY(OsmPos->X, OsmPos->Y);
		}

		FVector WorldPos = CoordConverter.OsmToUE(OsmPos->X, OsmPos->Y, ElevM);
		WorldPos.Z += Rule.ZOffset;

		float Scale = RandStream.FRandRange(Rule.ScaleRange.X, Rule.ScaleRange.Y);
		float Yaw = RandStream.FRandRange(Rule.YawRange.X, Rule.YawRange.Y);

		FTransform Transform;
		Transform.SetLocation(WorldPos);
		Transform.SetRotation(FQuat(FRotator(0, Yaw, 0)));
		Transform.SetScale3D(FVector(Scale));

		if (Rule.bUseInstancing)
		{
			UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(
				AssetContainer, Mesh,
				FString::Printf(TEXT("HISM_%s_%s"), *Rule.TagKey, *Rule.TagValue));

			HISM->AddInstance(Transform, true);
		}
		else
		{
			// Spawn individual static mesh actor
			FActorSpawnParameters AssetSpawnParams;
			AActor* AssetActor = World->SpawnActor<AActor>(WorldPos, FRotator(0, Yaw, 0), AssetSpawnParams);
			if (AssetActor)
			{
				UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(AssetActor, TEXT("Mesh"));
				MeshComp->SetStaticMesh(Mesh);
				MeshComp->SetWorldTransform(Transform);
				AssetActor->SetRootComponent(MeshComp);
				MeshComp->RegisterComponent();
			}
		}

		PlacedCount++;
	}

	UE_LOG(LogAssetPlacer, Log, TEXT("Placed %d assets from %d amenity nodes"), PlacedCount, Categories.AmenityNodeIds.Num());
}

void UOsm2MapAssetPlacer::PlaceAreaVegetation(
	UWorld* World,
	const FOsmRawData& RawData,
	const FOsmCategories& Categories,
	const TMap<int64, FVector2D>& ProjectedNodes,
	const FOsmCoordinateConverter& CoordConverter,
	UOsmAssetDictionary* Dictionary,
	UOsm2MapTerrainGenerator* TerrainGen)
{
	if (!Dictionary) return;

	// Find tree rule in dictionary
	TMap<FString, FString> TreeTags;
	TreeTags.Add(TEXT("natural"), TEXT("tree"));
	int32 TreeRuleIdx = Dictionary->FindMatchingRule(TreeTags);
	if (TreeRuleIdx == INDEX_NONE)
	{
		UE_LOG(LogAssetPlacer, Warning, TEXT("No tree rule found in dictionary, skipping area vegetation"));
		return;
	}

	const FOsmAssetMappingRule& TreeRule = Dictionary->Rules[TreeRuleIdx];

	// Create container actor
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(TEXT("OsmVegetation"));
	AActor* VegContainer = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!VegContainer) return;

	USceneComponent* Root = NewObject<USceneComponent>(VegContainer, TEXT("Root"));
	VegContainer->SetRootComponent(Root);
	Root->RegisterComponent();

	int32 TreeCount = 0;

	for (int64 AreaId : Categories.NaturalAreaIds)
	{
		const FOsmWayData* Way = RawData.Ways.Find(AreaId);
		if (!Way || Way->NodeRefs.Num() < 4) continue;

		// Determine density
		FString LanduseType = Way->GetTag(TEXT("landuse"));
		FString NaturalType = Way->GetTag(TEXT("natural"));
		FString LeisureType = Way->GetTag(TEXT("leisure"));

		float MinDistance = 15.0f; // Default park density
		if (NaturalType == TEXT("wood") || LanduseType == TEXT("forest"))
		{
			MinDistance = 5.0f; // Dense forest
		}

		// Build polygon from projected nodes
		TArray<FVector2D> Polygon;
		for (int64 NodeRef : Way->NodeRefs)
		{
			const FVector2D* Pos = ProjectedNodes.Find(NodeRef);
			if (Pos) Polygon.Add(*Pos);
		}
		if (Polygon.Num() < 3) continue;

		// Remove closing duplicate
		if (Polygon.Num() >= 2 && FVector2D::Distance(Polygon[0], Polygon.Last()) < 0.01)
		{
			Polygon.Pop();
		}

		// Compute bounding box
		FVector2D Min(TNumericLimits<double>::Max());
		FVector2D Max(TNumericLimits<double>::Lowest());
		for (const FVector2D& P : Polygon)
		{
			Min.X = FMath::Min(Min.X, P.X);
			Min.Y = FMath::Min(Min.Y, P.Y);
			Max.X = FMath::Max(Max.X, P.X);
			Max.Y = FMath::Max(Max.Y, P.Y);
		}

		// Poisson disk sampling
		TArray<FVector2D> SamplePoints = PoissonDiskSample(Min, Max, MinDistance);

		for (const FVector2D& SamplePos : SamplePoints)
		{
			if (!IsPointInPolygon(SamplePos, Polygon)) continue;

			// Deterministic random from position
			int32 Seed = static_cast<int32>(SamplePos.X * 1000.0 + SamplePos.Y * 7919.0);
			FRandomStream Rand(Seed);

			int32 VariantIdx = Rand.RandRange(0, TreeRule.MeshVariants.Num() - 1);
			UStaticMesh* Mesh = TreeRule.MeshVariants[VariantIdx].LoadSynchronous();
			if (!Mesh) continue;

			float ElevM = 0.0f;
			if (TerrainGen)
			{
				ElevM = TerrainGen->GetElevationAtOsmXY(SamplePos.X, SamplePos.Y);
			}

			FVector WorldPos = CoordConverter.OsmToUE(SamplePos.X, SamplePos.Y, ElevM);
			float Scale = Rand.FRandRange(TreeRule.ScaleRange.X, TreeRule.ScaleRange.Y);
			float Yaw = Rand.FRandRange(0.0f, 360.0f);

			FTransform Transform;
			Transform.SetLocation(WorldPos);
			Transform.SetRotation(FQuat(FRotator(0, Yaw, 0)));
			Transform.SetScale3D(FVector(Scale));

			UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(
				VegContainer, Mesh, TEXT("HISM_Trees"));
			HISM->AddInstance(Transform, true);

			TreeCount++;
		}
	}

	UE_LOG(LogAssetPlacer, Log, TEXT("Placed %d trees in %d natural areas"), TreeCount, Categories.NaturalAreaIds.Num());
}

UHierarchicalInstancedStaticMeshComponent* UOsm2MapAssetPlacer::GetOrCreateHISM(
	AActor* Owner, UStaticMesh* Mesh, const FString& Name)
{
	FString Key = Mesh->GetPathName() + TEXT("_") + Name;

	if (UHierarchicalInstancedStaticMeshComponent** Found = HISMCache.Find(Key))
	{
		return *Found;
	}

	UHierarchicalInstancedStaticMeshComponent* HISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(
		Owner, FName(*Name));
	HISM->SetStaticMesh(Mesh);
	HISM->SetupAttachment(Owner->GetRootComponent());
	HISM->RegisterComponent();

	HISMCache.Add(Key, HISM);
	return HISM;
}

TArray<FVector2D> UOsm2MapAssetPlacer::PoissonDiskSample(
	const FVector2D& Min, const FVector2D& Max,
	float MinDistance, int32 MaxAttempts) const
{
	TArray<FVector2D> Points;

	float CellSize = MinDistance / FMath::Sqrt(2.0f);
	int32 GridW = FMath::CeilToInt32((Max.X - Min.X) / CellSize);
	int32 GridH = FMath::CeilToInt32((Max.Y - Min.Y) / CellSize);

	if (GridW <= 0 || GridH <= 0 || GridW > 10000 || GridH > 10000)
	{
		return Points;
	}

	TArray<int32> Grid;
	Grid.SetNumZeroed(GridW * GridH);

	TArray<int32> ActiveList;
	FRandomStream Rand(42);

	// First point
	FVector2D FirstPoint(
		Rand.FRandRange(Min.X, Max.X),
		Rand.FRandRange(Min.Y, Max.Y)
	);
	Points.Add(FirstPoint);
	ActiveList.Add(0);

	int32 GX = FMath::Clamp(FMath::FloorToInt32((FirstPoint.X - Min.X) / CellSize), 0, GridW - 1);
	int32 GY = FMath::Clamp(FMath::FloorToInt32((FirstPoint.Y - Min.Y) / CellSize), 0, GridH - 1);
	Grid[GY * GridW + GX] = 1; // 1-indexed (0 = empty)

	while (ActiveList.Num() > 0)
	{
		int32 ActiveIdx = Rand.RandRange(0, ActiveList.Num() - 1);
		int32 PointIdx = ActiveList[ActiveIdx];
		const FVector2D& BasePoint = Points[PointIdx];

		bool bFound = false;
		for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
		{
			float Angle = Rand.FRandRange(0.0f, 6.28318530718f); // 2*PI radians
			float Dist = Rand.FRandRange(MinDistance, MinDistance * 2.0f);
			FVector2D Candidate(
				BasePoint.X + Dist * FMath::Cos(Angle),
				BasePoint.Y + Dist * FMath::Sin(Angle)
			);

			// Bounds check
			if (Candidate.X < Min.X || Candidate.X > Max.X ||
				Candidate.Y < Min.Y || Candidate.Y > Max.Y)
			{
				continue;
			}

			int32 CX = FMath::Clamp(FMath::FloorToInt32((Candidate.X - Min.X) / CellSize), 0, GridW - 1);
			int32 CY = FMath::Clamp(FMath::FloorToInt32((Candidate.Y - Min.Y) / CellSize), 0, GridH - 1);

			// Check neighborhood
			bool bTooClose = false;
			for (int32 dy = -2; dy <= 2 && !bTooClose; ++dy)
			{
				for (int32 dx = -2; dx <= 2 && !bTooClose; ++dx)
				{
					int32 NX = CX + dx;
					int32 NY = CY + dy;
					if (NX < 0 || NX >= GridW || NY < 0 || NY >= GridH) continue;

					int32 NeighborIdx = Grid[NY * GridW + NX];
					if (NeighborIdx > 0)
					{
						float D = FVector2D::Distance(Candidate, Points[NeighborIdx - 1]);
						if (D < MinDistance)
						{
							bTooClose = true;
						}
					}
				}
			}

			if (!bTooClose)
			{
				int32 NewIdx = Points.Num();
				Points.Add(Candidate);
				ActiveList.Add(NewIdx);
				Grid[CY * GridW + CX] = NewIdx + 1;
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			ActiveList.RemoveAtSwap(ActiveIdx);
		}
	}

	return Points;
}

bool UOsm2MapAssetPlacer::IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon)
{
	bool bInside = false;
	int32 N = Polygon.Num();
	for (int32 i = 0, j = N - 1; i < N; j = i++)
	{
		if (((Polygon[i].Y > Point.Y) != (Polygon[j].Y > Point.Y)) &&
			(Point.X < (Polygon[j].X - Polygon[i].X) * (Point.Y - Polygon[i].Y) / (Polygon[j].Y - Polygon[i].Y) + Polygon[i].X))
		{
			bInside = !bInside;
		}
	}
	return bInside;
}
