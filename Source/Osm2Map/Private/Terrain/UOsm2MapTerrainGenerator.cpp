#include "Terrain/UOsm2MapTerrainGenerator.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeEditorModule.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogOsmTerrain, Log, All);

ALandscape* UOsm2MapTerrainGenerator::Generate(
	UWorld* World,
	const FOsmRawData& RawData,
	const FOsmCategories& Categories,
	const TMap<int64, FVector2D>& ProjectedNodes,
	const FOsmCoordinateConverter& CoordConverter,
	const FString& ElevationPath,
	int32 LandscapeResolution)
{
#if !WITH_EDITOR
	UE_LOG(LogOsmTerrain, Error, TEXT("Landscape generation requires the editor"));
	return nullptr;
#else
	StoredMinLat = RawData.MinLat;
	StoredMaxLat = RawData.MaxLat;
	StoredMinLon = RawData.MinLon;
	StoredMaxLon = RawData.MaxLon;

	// Load elevation data if path provided
	bool bHasElevation = false;
	if (!ElevationPath.IsEmpty())
	{
		bHasElevation = ElevationProvider.LoadFromDirectory(
			ElevationPath, RawData.MinLat, RawData.MaxLat, RawData.MinLon, RawData.MaxLon);

		if (!bHasElevation)
		{
			UE_LOG(LogOsmTerrain, Warning, TEXT("No elevation data found in %s, using flat terrain"), *ElevationPath);
		}
	}

	// Generate heightmap
	TArray<uint16> Heightmap = ElevationProvider.GenerateHeightmap(
		LandscapeResolution, RawData.MinLat, RawData.MaxLat, RawData.MinLon, RawData.MaxLon);

	// Compute osm coordinate bounds for heightmap pixel mapping
	double MinOsmX = TNumericLimits<double>::Max();
	double MaxOsmX = TNumericLimits<double>::Lowest();
	double MinOsmY = TNumericLimits<double>::Max();
	double MaxOsmY = TNumericLimits<double>::Lowest();

	for (const auto& [Id, Pos] : ProjectedNodes)
	{
		MinOsmX = FMath::Min(MinOsmX, Pos.X);
		MaxOsmX = FMath::Max(MaxOsmX, Pos.X);
		MinOsmY = FMath::Min(MinOsmY, Pos.Y);
		MaxOsmY = FMath::Max(MaxOsmY, Pos.Y);
	}

	// Sculpt natural features into heightmap BEFORE creating landscape
	CarveRivers(Heightmap, LandscapeResolution, RawData, Categories, ProjectedNodes,
		MinOsmX, MaxOsmX, MinOsmY, MaxOsmY);

	FlattenWaterBodies(Heightmap, LandscapeResolution, RawData, Categories, ProjectedNodes,
		MinOsmX, MaxOsmX, MinOsmY, MaxOsmY);

	// Calculate landscape dimensions in UE units
	double WorldWidthM = MaxOsmX - MinOsmX;
	double WorldHeightM = MaxOsmY - MinOsmY;
	float Scale = CoordConverter.GetScale();

	// Determine component layout
	// Valid configurations: QuadsPerSection * SectionsPerComponent * NumComponents + 1 = Resolution
	int32 QuadsPerSection = 63;
	int32 SectionsPerComponent = 1;
	int32 NumComponentsX = FMath::Max(1, (LandscapeResolution - 1) / (QuadsPerSection * SectionsPerComponent));
	int32 NumComponentsY = NumComponentsX;

	// Adjust resolution to fit component layout exactly
	int32 AdjustedResolution = NumComponentsX * QuadsPerSection * SectionsPerComponent + 1;
	if (AdjustedResolution != LandscapeResolution)
	{
		UE_LOG(LogOsmTerrain, Warning, TEXT("Adjusted landscape resolution from %d to %d to fit component layout (%dx%d components)"),
			LandscapeResolution, AdjustedResolution, NumComponentsX, NumComponentsY);

		Heightmap = ElevationProvider.GenerateHeightmap(
			AdjustedResolution, RawData.MinLat, RawData.MaxLat, RawData.MinLon, RawData.MaxLon);

		CarveRivers(Heightmap, AdjustedResolution, RawData, Categories, ProjectedNodes,
			MinOsmX, MaxOsmX, MinOsmY, MaxOsmY);
		FlattenWaterBodies(Heightmap, AdjustedResolution, RawData, Categories, ProjectedNodes,
			MinOsmX, MaxOsmX, MinOsmY, MaxOsmY);
	}

	// Landscape scale
	FVector LandscapeScale;
	LandscapeScale.X = (WorldHeightM * Scale) / FMath::Max(1, AdjustedResolution - 1);
	LandscapeScale.Y = (WorldWidthM * Scale) / FMath::Max(1, AdjustedResolution - 1);

	float ElevRange = bHasElevation ?
		(ElevationProvider.GetMaxElevation() - ElevationProvider.GetMinElevation()) : 1.0f;
	LandscapeScale.Z = FMath::Max(0.01f, (ElevRange * Scale) / 512.0f);

	UE_LOG(LogOsmTerrain, Log, TEXT("Creating landscape: %dx%d, scale (%.2f, %.2f, %.2f), extent %.0fm x %.0fm"),
		AdjustedResolution, AdjustedResolution,
		LandscapeScale.X, LandscapeScale.Y, LandscapeScale.Z,
		WorldWidthM, WorldHeightM);

	// Position landscape so center is at world origin
	FVector LandscapeLocation(
		-(AdjustedResolution - 1) * LandscapeScale.X * 0.5f,
		-(AdjustedResolution - 1) * LandscapeScale.Y * 0.5f,
		0.0f
	);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(TEXT("OsmLandscape"));
	ALandscape* Landscape = World->SpawnActor<ALandscape>(LandscapeLocation, FRotator::ZeroRotator, SpawnParams);

	if (!Landscape)
	{
		UE_LOG(LogOsmTerrain, Error, TEXT("Failed to spawn ALandscape actor"));
		return nullptr;
	}

	Landscape->SetActorScale3D(LandscapeScale);

	// Import heightmap via the Landscape API
	// UE 5.6 expects TMap<FGuid, TArray<uint16>> for height data
	FGuid HeightLayerGuid = FGuid::NewGuid();
	TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
	HeightDataPerLayer.Add(HeightLayerGuid, MoveTemp(Heightmap));

	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;

	Landscape->Import(
		FGuid::NewGuid(),
		0, 0,
		AdjustedResolution - 1, AdjustedResolution - 1,
		SectionsPerComponent, QuadsPerSection,
		HeightDataPerLayer,
		nullptr,
		MaterialLayerDataPerLayer,
		ELandscapeImportAlphamapType::Additive
	);

	UE_LOG(LogOsmTerrain, Log, TEXT("Landscape created successfully: %dx%d components"), NumComponentsX, NumComponentsY);

	return Landscape;
#endif // WITH_EDITOR
}

float UOsm2MapTerrainGenerator::GetElevationAtOsmXY(double OsmX, double OsmY) const
{
	if (!ElevationProvider.IsLoaded())
	{
		return 0.0f;
	}

	double CenterLat = (StoredMinLat + StoredMaxLat) * 0.5;
	double CenterLon = (StoredMinLon + StoredMaxLon) * 0.5;
	constexpr double R = 6371000.0;
	constexpr double LocalPI = 3.14159265358979323846;
	constexpr double DEG2RAD = LocalPI / 180.0;

	double CosLat = FMath::Cos(CenterLat * DEG2RAD);
	double Lat = CenterLat + (OsmY / (R * DEG2RAD));
	double Lon = CenterLon + (OsmX / (R * CosLat * DEG2RAD));

	return ElevationProvider.SampleElevation(Lat, Lon);
}

void UOsm2MapTerrainGenerator::CarveRivers(
	TArray<uint16>& Heightmap, int32 Resolution,
	const FOsmRawData& RawData, const FOsmCategories& Categories,
	const TMap<int64, FVector2D>& ProjectedNodes,
	double MinOsmX, double MaxOsmX, double MinOsmY, double MaxOsmY)
{
	constexpr double LocalPI = 3.14159265358979323846;
	double OsmWidth = MaxOsmX - MinOsmX;
	double OsmHeight = MaxOsmY - MinOsmY;
	if (OsmWidth <= 0 || OsmHeight <= 0) return;

	for (int64 WayId : Categories.WaterwayIds)
	{
		const FOsmWayData* Way = RawData.Ways.Find(WayId);
		if (!Way || Way->NodeRefs.Num() < 2) continue;

		FString WaterwayType = Way->GetTag(TEXT("waterway"));
		if (WaterwayType.IsEmpty()) continue;

		float RiverWidthM = GetDefaultRiverWidth(WaterwayType);

		FString WidthTag = Way->GetTag(TEXT("width"));
		if (!WidthTag.IsEmpty())
		{
			RiverWidthM = FCString::Atof(*WidthTag);
		}

		float DepthM = FMath::Clamp(RiverWidthM * 0.2f, 1.0f, 5.0f);

		for (int32 i = 0; i < Way->NodeRefs.Num() - 1; ++i)
		{
			const FVector2D* P0 = ProjectedNodes.Find(Way->NodeRefs[i]);
			const FVector2D* P1 = ProjectedNodes.Find(Way->NodeRefs[i + 1]);
			if (!P0 || !P1) continue;

			float PadM = RiverWidthM;
			double SegMinX = FMath::Min(P0->X, P1->X) - PadM;
			double SegMaxX = FMath::Max(P0->X, P1->X) + PadM;
			double SegMinY = FMath::Min(P0->Y, P1->Y) - PadM;
			double SegMaxY = FMath::Max(P0->Y, P1->Y) + PadM;

			int32 PixMinX = FMath::Max(0, FMath::FloorToInt32((SegMinX - MinOsmX) / OsmWidth * (Resolution - 1)));
			int32 PixMaxX = FMath::Min(Resolution - 1, FMath::CeilToInt32((SegMaxX - MinOsmX) / OsmWidth * (Resolution - 1)));
			int32 PixMinY = FMath::Max(0, FMath::FloorToInt32((1.0 - (SegMaxY - MinOsmY) / OsmHeight) * (Resolution - 1)));
			int32 PixMaxY = FMath::Min(Resolution - 1, FMath::CeilToInt32((1.0 - (SegMinY - MinOsmY) / OsmHeight) * (Resolution - 1)));

			FVector2D SegDir = *P1 - *P0;
			double SegLen = SegDir.Size();
			if (SegLen < 0.01) continue;
			SegDir /= SegLen;

			for (int32 PY = PixMinY; PY <= PixMaxY; ++PY)
			{
				for (int32 PX = PixMinX; PX <= PixMaxX; ++PX)
				{
					double OsmX = MinOsmX + (static_cast<double>(PX) / (Resolution - 1)) * OsmWidth;
					double OsmY = MaxOsmY - (static_cast<double>(PY) / (Resolution - 1)) * OsmHeight;

					FVector2D ToPoint(OsmX - P0->X, OsmY - P0->Y);
					double Proj = FVector2D::DotProduct(ToPoint, SegDir);
					Proj = FMath::Clamp(Proj, 0.0, SegLen);
					FVector2D Closest = *P0 + SegDir * Proj;
					double Dist = FVector2D::Distance(FVector2D(OsmX, OsmY), Closest);

					float HalfWidth = RiverWidthM * 0.5f;
					if (Dist < HalfWidth)
					{
						float T = static_cast<float>(Dist / HalfWidth);
						float Falloff = 0.5f * (1.0f + FMath::Cos(T * LocalPI));

						int32 Idx = PY * Resolution + PX;
						float CurrentHeight = static_cast<float>(Heightmap[Idx]);

						float ElevRange = FMath::Max(1.0f, ElevationProvider.GetMaxElevation() - ElevationProvider.GetMinElevation());
						float DepthInHeightmapUnits = (DepthM / ElevRange) * 65535.0f;

						float NewHeight = CurrentHeight - Falloff * DepthInHeightmapUnits;
						Heightmap[Idx] = static_cast<uint16>(FMath::Clamp(NewHeight, 0.0f, 65535.0f));
					}
				}
			}
		}
	}
}

void UOsm2MapTerrainGenerator::FlattenWaterBodies(
	TArray<uint16>& Heightmap, int32 Resolution,
	const FOsmRawData& RawData, const FOsmCategories& Categories,
	const TMap<int64, FVector2D>& ProjectedNodes,
	double MinOsmX, double MaxOsmX, double MinOsmY, double MaxOsmY)
{
	double OsmWidth = MaxOsmX - MinOsmX;
	double OsmHeight = MaxOsmY - MinOsmY;
	if (OsmWidth <= 0 || OsmHeight <= 0) return;

	for (int64 WayId : Categories.WaterwayIds)
	{
		const FOsmWayData* Way = RawData.Ways.Find(WayId);
		if (!Way || Way->NodeRefs.Num() < 4) continue;

		FString Natural = Way->GetTag(TEXT("natural"));
		if (Natural != TEXT("water")) continue;

		if (Way->NodeRefs[0] != Way->NodeRefs.Last()) continue;

		TArray<FVector2D> Polygon;
		for (int64 NodeRef : Way->NodeRefs)
		{
			const FVector2D* Pos = ProjectedNodes.Find(NodeRef);
			if (Pos) Polygon.Add(*Pos);
		}
		if (Polygon.Num() < 3) continue;

		FVector2D PolyMin(TNumericLimits<double>::Max());
		FVector2D PolyMax(TNumericLimits<double>::Lowest());
		for (const FVector2D& P : Polygon)
		{
			PolyMin.X = FMath::Min(PolyMin.X, P.X);
			PolyMin.Y = FMath::Min(PolyMin.Y, P.Y);
			PolyMax.X = FMath::Max(PolyMax.X, P.X);
			PolyMax.Y = FMath::Max(PolyMax.Y, P.Y);
		}

		int32 PixMinX = FMath::Max(0, FMath::FloorToInt32((PolyMin.X - MinOsmX) / OsmWidth * (Resolution - 1)));
		int32 PixMaxX = FMath::Min(Resolution - 1, FMath::CeilToInt32((PolyMax.X - MinOsmX) / OsmWidth * (Resolution - 1)));
		int32 PixMinY = FMath::Max(0, FMath::FloorToInt32((1.0 - (PolyMax.Y - MinOsmY) / OsmHeight) * (Resolution - 1)));
		int32 PixMaxY = FMath::Min(Resolution - 1, FMath::CeilToInt32((1.0 - (PolyMin.Y - MinOsmY) / OsmHeight) * (Resolution - 1)));

		float SumHeight = 0.0f;
		int32 SampleCount = 0;

		for (int32 PY = PixMinY; PY <= PixMaxY; ++PY)
		{
			for (int32 PX = PixMinX; PX <= PixMaxX; ++PX)
			{
				double OsmX = MinOsmX + (static_cast<double>(PX) / (Resolution - 1)) * OsmWidth;
				double OsmY = MaxOsmY - (static_cast<double>(PY) / (Resolution - 1)) * OsmHeight;

				if (IsPointInPolygon(FVector2D(OsmX, OsmY), Polygon))
				{
					SumHeight += static_cast<float>(Heightmap[PY * Resolution + PX]);
					SampleCount++;
				}
			}
		}

		if (SampleCount == 0) continue;
		uint16 WaterLevel = static_cast<uint16>(SumHeight / SampleCount);

		for (int32 PY = PixMinY; PY <= PixMaxY; ++PY)
		{
			for (int32 PX = PixMinX; PX <= PixMaxX; ++PX)
			{
				double OsmX = MinOsmX + (static_cast<double>(PX) / (Resolution - 1)) * OsmWidth;
				double OsmY = MaxOsmY - (static_cast<double>(PY) / (Resolution - 1)) * OsmHeight;

				if (IsPointInPolygon(FVector2D(OsmX, OsmY), Polygon))
				{
					Heightmap[PY * Resolution + PX] = WaterLevel;
				}
			}
		}
	}
}

bool UOsm2MapTerrainGenerator::IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon)
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

float UOsm2MapTerrainGenerator::GetDefaultRiverWidth(const FString& WaterwayType)
{
	if (WaterwayType == TEXT("river")) return 15.0f;
	if (WaterwayType == TEXT("canal")) return 8.0f;
	if (WaterwayType == TEXT("stream")) return 3.0f;
	if (WaterwayType == TEXT("drain")) return 2.0f;
	if (WaterwayType == TEXT("ditch")) return 1.5f;
	return 5.0f;
}
