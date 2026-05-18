#include "Roads/UOsm2MapRoadGenerator.h"
#include "Terrain/UOsm2MapTerrainGenerator.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// osm2xodr includes
#include "osm/OsmTypes.h"
#include "osm/OsmParser.h"
#include "osm/OsmFilter.h"
#include "conversion/Projection.h"
#include "conversion/RoadNetwork.h"
#include "xodr/XodrTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogRoadGen, Log, All);

void UOsm2MapRoadGenerator::Generate(
	UWorld* World,
	const FOsmRawData& RawData,
	const FOsmCategories& Categories,
	const TMap<int64, FVector2D>& ProjectedNodes,
	const FOsmCoordinateConverter& CoordConverter,
	float RoadZOffset,
	UOsm2MapTerrainGenerator* TerrainGen)
{
	// Convert back to native OsmData for osm2xodr processing
	OsmData NativeData = RawData.ToOsmData();

	// Filter to drivable roads only (on a copy)
	::OsmFilter Filter;
	Filter.apply(NativeData);

	// Project coordinates
	::Projection Proj(NativeData.minLat, NativeData.maxLat, NativeData.minLon, NativeData.maxLon);
	NodeXYMap NodePositions = Proj.projectAll(NativeData.nodes);

	// Build road network with lane information
	::RoadNetwork Builder;
	XodrNetwork Network = Builder.build(NativeData, NodePositions);

	UE_LOG(LogRoadGen, Log, TEXT("Built road network: %d roads, %d junctions"),
		Network.roads.size(), Network.junctions.size());

	// Convert ZOffset from cm to meters
	float ZOffsetMeters = RoadZOffset / CoordConverter.GetScale();

	// Generate road meshes
	int32 RoadCount = 0;
	for (const XodrRoad& Road : Network.roads)
	{
		if (Road.junctionId != -1) continue; // Skip junction internal roads for now

		AActor* RoadActor = GenerateRoadMesh(World, Road, CoordConverter, ZOffsetMeters, TerrainGen);
		if (RoadActor)
		{
			RoadCount++;
		}
	}

	// Generate junction meshes
	int32 JunctionCount = 0;
	for (const XodrJunction& Junction : Network.junctions)
	{
		AActor* JunctionActor = GenerateJunctionMesh(World, Junction, Network, CoordConverter, ZOffsetMeters, TerrainGen);
		if (JunctionActor)
		{
			JunctionCount++;
		}
	}

	UE_LOG(LogRoadGen, Log, TEXT("Generated %d road segments, %d junctions"), RoadCount, JunctionCount);
}

TArray<UOsm2MapRoadGenerator::FRoadSample> UOsm2MapRoadGenerator::SampleReferenceLine(
	const XodrRoad& Road, double StepSize) const
{
	TArray<FRoadSample> Samples;

	if (Road.geometries.empty()) return Samples;

	double TotalLength = Road.length;
	int32 NumSamples = FMath::Max(2, FMath::CeilToInt32(TotalLength / StepSize) + 1);

	for (int32 i = 0; i < NumSamples; ++i)
	{
		double S = (i == NumSamples - 1) ? TotalLength : (i * StepSize);
		S = FMath::Min(S, TotalLength);

		// Find the geometry element containing this S value
		const XodrGeometry* ActiveGeom = &Road.geometries[0];
		for (int32 g = static_cast<int32>(Road.geometries.size()) - 1; g >= 0; --g)
		{
			if (S >= Road.geometries[g].s)
			{
				ActiveGeom = &Road.geometries[g];
				break;
			}
		}

		// Linear interpolation along the geometry element
		double ds = S - ActiveGeom->s;
		FRoadSample Sample;
		Sample.S = S;
		Sample.Heading = ActiveGeom->hdg;
		Sample.Position.X = ActiveGeom->x + ds * FMath::Cos(ActiveGeom->hdg);
		Sample.Position.Y = ActiveGeom->y + ds * FMath::Sin(ActiveGeom->hdg);

		Samples.Add(Sample);
	}

	return Samples;
}

void UOsm2MapRoadGenerator::ComputeLaneEdges(
	const XodrRoad& Road, double S,
	TArray<FLaneEdge>& OutLeftEdges, TArray<FLaneEdge>& OutRightEdges) const
{
	// Find active lane section for this S
	const XodrLaneSection* ActiveSection = nullptr;
	for (int32 i = static_cast<int32>(Road.laneSections.size()) - 1; i >= 0; --i)
	{
		if (S >= Road.laneSections[i].s)
		{
			ActiveSection = &Road.laneSections[i];
			break;
		}
	}

	if (!ActiveSection) return;

	double ds = S - ActiveSection->s;

	// Left lanes (id > 0, inner to outer)
	double CumulativeOffset = 0.0;
	for (const XodrLane& Lane : ActiveSection->leftLanes)
	{
		double Width = 0.0;
		if (!Lane.widths.empty())
		{
			// Find active width entry
			const XodrLane::Width* ActiveWidth = &Lane.widths[0];
			for (int32 w = static_cast<int32>(Lane.widths.size()) - 1; w >= 0; --w)
			{
				if (ds >= Lane.widths[w].sOffset)
				{
					ActiveWidth = &Lane.widths[w];
					break;
				}
			}
			double dw = ds - ActiveWidth->sOffset;
			Width = ActiveWidth->a + ActiveWidth->b * dw + ActiveWidth->c * dw * dw + ActiveWidth->d * dw * dw * dw;
		}

		CumulativeOffset += Width;
		FLaneEdge Edge;
		Edge.Offset = CumulativeOffset;
		Edge.LaneType = FString(UTF8_TO_TCHAR(Lane.type.c_str()));
		OutLeftEdges.Add(Edge);
	}

	// Right lanes (id < 0, inner to outer)
	CumulativeOffset = 0.0;
	for (const XodrLane& Lane : ActiveSection->rightLanes)
	{
		double Width = 0.0;
		if (!Lane.widths.empty())
		{
			const XodrLane::Width* ActiveWidth = &Lane.widths[0];
			for (int32 w = static_cast<int32>(Lane.widths.size()) - 1; w >= 0; --w)
			{
				if (ds >= Lane.widths[w].sOffset)
				{
					ActiveWidth = &Lane.widths[w];
					break;
				}
			}
			double dw = ds - ActiveWidth->sOffset;
			Width = ActiveWidth->a + ActiveWidth->b * dw + ActiveWidth->c * dw * dw + ActiveWidth->d * dw * dw * dw;
		}

		CumulativeOffset += Width;
		FLaneEdge Edge;
		Edge.Offset = -CumulativeOffset; // Negative for right side
		Edge.LaneType = FString(UTF8_TO_TCHAR(Lane.type.c_str()));
		OutRightEdges.Add(Edge);
	}
}

AActor* UOsm2MapRoadGenerator::GenerateRoadMesh(
	UWorld* World,
	const XodrRoad& Road,
	const FOsmCoordinateConverter& CoordConverter,
	float ZOffset,
	UOsm2MapTerrainGenerator* TerrainGen)
{
	TArray<FRoadSample> Samples = SampleReferenceLine(Road, 2.0);
	if (Samples.Num() < 2) return nullptr;

	// Spawn actor
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(*FString::Printf(TEXT("Road_%d"), Road.id));
	AActor* RoadActor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!RoadActor) return nullptr;

	USceneComponent* Root = NewObject<USceneComponent>(RoadActor, TEXT("Root"));
	RoadActor->SetRootComponent(Root);
	Root->RegisterComponent();

	UProceduralMeshComponent* MeshComp = NewObject<UProceduralMeshComponent>(RoadActor, TEXT("RoadMesh"));
	MeshComp->SetupAttachment(Root);
	MeshComp->RegisterComponent();

	// Build mesh geometry
	// For each sample point, compute all lane edge positions
	// Then generate quad strips between consecutive samples for each lane

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;

	int32 SectionIdx = 0; // Section 0 = driving lanes, 1 = sidewalks

	// Collect all driving lane vertices first
	TArray<TArray<FVector>> LaneStrips; // [lane_edge_index][sample_index] -> 3D position

	// Determine lane structure from first sample
	TArray<FLaneEdge> LeftEdges, RightEdges;
	ComputeLaneEdges(Road, 0.0, LeftEdges, RightEdges);

	// Total lane edges: left + center(0) + right
	int32 NumEdges = LeftEdges.Num() + 1 + RightEdges.Num();
	LaneStrips.SetNum(NumEdges);
	for (auto& Strip : LaneStrips)
	{
		Strip.Reserve(Samples.Num());
	}

	for (const FRoadSample& Sample : Samples)
	{
		TArray<FLaneEdge> Left, Right;
		ComputeLaneEdges(Road, Sample.S, Left, Right);

		// Normal perpendicular to heading (points left)
		double Nx = -FMath::Sin(Sample.Heading);
		double Ny = FMath::Cos(Sample.Heading);

		// Get terrain elevation
		float ElevM = ZOffset;
		if (TerrainGen)
		{
			ElevM += TerrainGen->GetElevationAtOsmXY(Sample.Position.X, Sample.Position.Y);
		}

		// Compute 3D positions for each lane edge
		// Order: rightmost -> center -> leftmost
		int32 EdgeIdx = 0;

		// Right edges (outer to inner = reversed)
		for (int32 r = Right.Num() - 1; r >= 0; --r)
		{
			double OsmX = Sample.Position.X + Right[r].Offset * Nx;
			double OsmY = Sample.Position.Y + Right[r].Offset * Ny;
			LaneStrips[EdgeIdx++].Add(CoordConverter.OsmToUE(OsmX, OsmY, ElevM));
		}

		// Center line
		LaneStrips[EdgeIdx++].Add(CoordConverter.OsmToUE(Sample.Position.X, Sample.Position.Y, ElevM));

		// Left edges (inner to outer)
		for (int32 l = 0; l < Left.Num(); ++l)
		{
			double OsmX = Sample.Position.X + Left[l].Offset * Nx;
			double OsmY = Sample.Position.Y + Left[l].Offset * Ny;
			LaneStrips[EdgeIdx++].Add(CoordConverter.OsmToUE(OsmX, OsmY, ElevM));
		}
	}

	// Generate quad strips between adjacent lane edge strips
	for (int32 Strip = 0; Strip < LaneStrips.Num() - 1; ++Strip)
	{
		const TArray<FVector>& LeftStrip = LaneStrips[Strip + 1];
		const TArray<FVector>& RightStrip = LaneStrips[Strip];

		int32 NumSamples = FMath::Min(LeftStrip.Num(), RightStrip.Num());
		if (NumSamples < 2) continue;

		int32 BaseVertex = Vertices.Num();

		for (int32 S = 0; S < NumSamples; ++S)
		{
			float V = static_cast<float>(S) / (NumSamples - 1);

			Vertices.Add(RightStrip[S]);
			Normals.Add(FVector(0, 0, 1)); // Up-facing road surface
			UVs.Add(FVector2D(0.0f, V));

			Vertices.Add(LeftStrip[S]);
			Normals.Add(FVector(0, 0, 1));
			UVs.Add(FVector2D(1.0f, V));
		}

		// Triangulate quad strip
		for (int32 S = 0; S < NumSamples - 1; ++S)
		{
			int32 BL = BaseVertex + S * 2;
			int32 BR = BL + 1;
			int32 TL = BL + 2;
			int32 TR = BL + 3;

			Triangles.Add(BL);
			Triangles.Add(TL);
			Triangles.Add(BR);

			Triangles.Add(BR);
			Triangles.Add(TL);
			Triangles.Add(TR);
		}
	}

	if (Vertices.Num() > 0)
	{
		MeshComp->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), true);
	}

#if WITH_EDITOR
	FString RoadName = Road.name.empty() ? FString::Printf(TEXT("Road_%d"), Road.id) : FString(UTF8_TO_TCHAR(Road.name.c_str()));
	RoadActor->SetActorLabel(RoadName);
#endif

	return RoadActor;
}

AActor* UOsm2MapRoadGenerator::GenerateJunctionMesh(
	UWorld* World,
	const XodrJunction& Junction,
	const XodrNetwork& Network,
	const FOsmCoordinateConverter& CoordConverter,
	float ZOffset,
	UOsm2MapTerrainGenerator* TerrainGen)
{
	// Collect all road endpoints at this junction
	TArray<FVector2D> EdgePoints; // In osm local meters

	for (const XodrJunctionConnection& Conn : Junction.connections)
	{
		// Find the connecting road
		const XodrRoad* ConnRoad = nullptr;
		for (const XodrRoad& R : Network.roads)
		{
			if (R.id == Conn.connectingRoad)
			{
				ConnRoad = &R;
				break;
			}
		}
		if (!ConnRoad || ConnRoad->geometries.empty()) continue;

		// Get start or end of the connecting road
		double S;
		double Heading;
		FVector2D Pos;

		if (Conn.contactPoint == XodrJunctionConnection::ContactPoint::Start)
		{
			S = 0.0;
			const auto& G = ConnRoad->geometries.front();
			Pos = FVector2D(G.x, G.y);
			Heading = G.hdg;
		}
		else
		{
			S = ConnRoad->length;
			const auto& G = ConnRoad->geometries.back();
			double ds = S - G.s;
			Pos.X = G.x + ds * FMath::Cos(G.hdg);
			Pos.Y = G.y + ds * FMath::Sin(G.hdg);
			Heading = G.hdg;
		}

		// Compute lane edges at this point
		TArray<FLaneEdge> Left, Right;
		ComputeLaneEdges(*ConnRoad, S, Left, Right);

		double Nx = -FMath::Sin(Heading);
		double Ny = FMath::Cos(Heading);

		// Add outermost edge points
		if (Right.Num() > 0)
		{
			double Offset = Right.Last().Offset;
			EdgePoints.Add(FVector2D(Pos.X + Offset * Nx, Pos.Y + Offset * Ny));
		}
		if (Left.Num() > 0)
		{
			double Offset = Left.Last().Offset;
			EdgePoints.Add(FVector2D(Pos.X + Offset * Nx, Pos.Y + Offset * Ny));
		}
	}

	if (EdgePoints.Num() < 3) return nullptr;

	// Sort points angularly around centroid
	FVector2D Centroid(0, 0);
	for (const FVector2D& P : EdgePoints)
	{
		Centroid += P;
	}
	Centroid /= EdgePoints.Num();

	EdgePoints.Sort([&Centroid](const FVector2D& A, const FVector2D& B)
	{
		double AngleA = FMath::Atan2(A.Y - Centroid.Y, A.X - Centroid.X);
		double AngleB = FMath::Atan2(B.Y - Centroid.Y, B.X - Centroid.X);
		return AngleA < AngleB;
	});

	// Get elevation at centroid
	float ElevM = ZOffset;
	if (TerrainGen)
	{
		ElevM += TerrainGen->GetElevationAtOsmXY(Centroid.X, Centroid.Y);
	}

	// Generate fan triangulation from centroid
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(*FString::Printf(TEXT("Junction_%d"), Junction.id));
	AActor* JunctionActor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!JunctionActor) return nullptr;

	USceneComponent* Root = NewObject<USceneComponent>(JunctionActor, TEXT("Root"));
	JunctionActor->SetRootComponent(Root);
	Root->RegisterComponent();

	UProceduralMeshComponent* MeshComp = NewObject<UProceduralMeshComponent>(JunctionActor, TEXT("JunctionMesh"));
	MeshComp->SetupAttachment(Root);
	MeshComp->RegisterComponent();

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> MeshNormals;
	TArray<FVector2D> MeshUVs;

	// Center vertex
	Vertices.Add(CoordConverter.OsmToUE(Centroid.X, Centroid.Y, ElevM));
	MeshNormals.Add(FVector(0, 0, 1));
	MeshUVs.Add(FVector2D(0.5f, 0.5f));

	// Edge vertices
	for (const FVector2D& P : EdgePoints)
	{
		Vertices.Add(CoordConverter.OsmToUE(P.X, P.Y, ElevM));
		MeshNormals.Add(FVector(0, 0, 1));

		FVector2D UV((P.X - Centroid.X) * 0.1f + 0.5f, (P.Y - Centroid.Y) * 0.1f + 0.5f);
		MeshUVs.Add(UV);
	}

	// Fan triangles
	for (int32 i = 0; i < EdgePoints.Num(); ++i)
	{
		int32 Next = (i + 1) % EdgePoints.Num();
		Triangles.Add(0);         // Center
		Triangles.Add(i + 1);     // Current edge
		Triangles.Add(Next + 1);  // Next edge
	}

	MeshComp->CreateMeshSection(0, Vertices, Triangles, MeshNormals, MeshUVs, TArray<FColor>(), TArray<FProcMeshTangent>(), true);

#if WITH_EDITOR
	JunctionActor->SetActorLabel(FString::Printf(TEXT("Junction_%d"), Junction.id));
#endif

	return JunctionActor;
}
