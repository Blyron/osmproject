#include "Pipeline/FOsmImportPipeline.h"
#include "Terrain/UOsm2MapTerrainGenerator.h"
#include "Roads/UOsm2MapRoadGenerator.h"
#include "Buildings/UOsm2MapBuildingGenerator.h"
#include "Buildings/FBuildingExtractor.h"
#include "Assets/UOsm2MapAssetPlacer.h"
#include "Assets/UOsmAssetDictionary.h"
#include "osm/OsmParser.h"
#include "osm/OsmFilter.h"
#include "conversion/Projection.h"
#include "conversion/RoadNetwork.h"
#include "Misc/ScopedSlowTask.h"

DEFINE_LOG_CATEGORY(LogOsm2Map);

bool UOsmImportPipeline::Execute(UWorld* World, const FOsmImportSettings& Settings)
{
	if (!World)
	{
		UE_LOG(LogOsm2Map, Error, TEXT("Invalid world"));
		return false;
	}

	FScopedSlowTask SlowTask(7.0f, NSLOCTEXT("Osm2Map", "Importing", "Importing OSM data..."));
	SlowTask.MakeDialog(true);

	// Stage 1: Parse
	SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("Osm2Map", "Parsing", "Parsing OSM file..."));
	if (!ParseOsmFile(Settings.OsmFilePath))
	{
		return false;
	}

	// Stage 2: Categorize
	SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("Osm2Map", "Categorizing", "Categorizing OSM data..."));
	if (!CategorizeData())
	{
		return false;
	}

	// Stage 3: Project coordinates
	SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("Osm2Map", "Projecting", "Projecting coordinates..."));
	if (!ProjectCoordinates(Settings.WorldScale))
	{
		return false;
	}

	// Stage 4: Terrain (must be first - other stages need Z from terrain)
	if (Settings.bImportTerrain)
	{
		SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("Osm2Map", "Terrain", "Generating terrain..."));
		GenerateTerrain(World, Settings);
	}
	else
	{
		SlowTask.EnterProgressFrame(1.0f);
	}

	// Stage 5: Roads
	if (Settings.bImportRoads)
	{
		SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("Osm2Map", "Roads", "Generating roads..."));
		GenerateRoads(World, Settings);
	}
	else
	{
		SlowTask.EnterProgressFrame(1.0f);
	}

	// Stage 6: Buildings
	if (Settings.bImportBuildings)
	{
		SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("Osm2Map", "Buildings", "Generating buildings..."));
		GenerateBuildings(World, Settings);
	}
	else
	{
		SlowTask.EnterProgressFrame(1.0f);
	}

	// Stage 7: Assets
	if (Settings.bImportAssets)
	{
		SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("Osm2Map", "Assets", "Placing assets..."));
		PlaceAssets(World, Settings);
	}
	else
	{
		SlowTask.EnterProgressFrame(1.0f);
	}

	UE_LOG(LogOsm2Map, Log, TEXT("OSM import completed successfully"));
	return true;
}

bool UOsmImportPipeline::ParseOsmFile(const FString& FilePath)
{
	if (FilePath.IsEmpty())
	{
		UE_LOG(LogOsm2Map, Error, TEXT("OSM file path is empty"));
		return false;
	}

	std::string StdPath = TCHAR_TO_UTF8(*FilePath);

	try
	{
		::OsmParser Parser;
		OsmData NativeData = Parser.parse(StdPath);

		RawData = FOsmRawData::FromOsmData(NativeData);

		UE_LOG(LogOsm2Map, Log, TEXT("Parsed OSM: %d nodes, %d ways, %d relations"),
			RawData.Nodes.Num(), RawData.Ways.Num(), RawData.Relations.Num());
		UE_LOG(LogOsm2Map, Log, TEXT("Bounds: lat [%.6f, %.6f], lon [%.6f, %.6f]"),
			RawData.MinLat, RawData.MaxLat, RawData.MinLon, RawData.MaxLon);

		return RawData.IsValid();
	}
	catch (const std::exception& Ex)
	{
		UE_LOG(LogOsm2Map, Error, TEXT("Failed to parse OSM file: %s"), UTF8_TO_TCHAR(Ex.what()));
		return false;
	}
}

bool UOsmImportPipeline::CategorizeData()
{
	Categories = FOsmCategorizer::Categorize(RawData);
	return true;
}

bool UOsmImportPipeline::ProjectCoordinates(float WorldScale)
{
	CoordConverter = FOsmCoordinateConverter(WorldScale);

	::Projection Proj(RawData.MinLat, RawData.MaxLat, RawData.MinLon, RawData.MaxLon);

	ProjectedNodes.Reserve(RawData.Nodes.Num());
	for (const auto& [Id, Node] : RawData.Nodes)
	{
		XY Pos = Proj.project(Node.Lat, Node.Lon);
		ProjectedNodes.Add(Id, FVector2D(Pos.x, Pos.y));
	}

	XY MinCorner = Proj.project(RawData.MinLat, RawData.MinLon);
	XY MaxCorner = Proj.project(RawData.MaxLat, RawData.MaxLon);
	double WidthM = MaxCorner.x - MinCorner.x;
	double HeightM = MaxCorner.y - MinCorner.y;

	UE_LOG(LogOsm2Map, Log, TEXT("Projected %d nodes. World extent: %.0fm x %.0fm (%.0f x %.0f UE units)"),
		ProjectedNodes.Num(), WidthM, HeightM, WidthM * WorldScale, HeightM * WorldScale);

	return true;
}

bool UOsmImportPipeline::GenerateTerrain(UWorld* World, const FOsmImportSettings& Settings)
{
	UOsm2MapTerrainGenerator* TerrainGen = NewObject<UOsm2MapTerrainGenerator>(this);
	TerrainGenerator = TerrainGen;

	ALandscape* Landscape = TerrainGen->Generate(
		World, RawData, Categories, ProjectedNodes, CoordConverter,
		Settings.ElevationDataPath, Settings.LandscapeResolution);

	return Landscape != nullptr;
}

bool UOsmImportPipeline::GenerateRoads(UWorld* World, const FOsmImportSettings& Settings)
{
	UOsm2MapRoadGenerator* RoadGen = NewObject<UOsm2MapRoadGenerator>(this);

	RoadGen->Generate(World, RawData, Categories, ProjectedNodes, CoordConverter,
		Settings.RoadZOffset, TerrainGenerator);

	return true;
}

bool UOsmImportPipeline::GenerateBuildings(UWorld* World, const FOsmImportSettings& Settings)
{
	// Extract footprints
	TArray<FBuildingFootprint> Footprints = FBuildingExtractor::Extract(
		RawData, Categories, ProjectedNodes, Settings.DefaultBuildingHeight);

	// Generate meshes
	UOsm2MapBuildingGenerator* BuildingGen = NewObject<UOsm2MapBuildingGenerator>(this);
	BuildingGen->Generate(World, Footprints, CoordConverter, TerrainGenerator);

	return true;
}

bool UOsmImportPipeline::PlaceAssets(UWorld* World, const FOsmImportSettings& Settings)
{
	UOsmAssetDictionary* Dictionary = Settings.AssetDictionary.LoadSynchronous();
	if (!Dictionary)
	{
		UE_LOG(LogOsm2Map, Warning, TEXT("No asset dictionary configured, skipping asset placement"));
		return true;
	}

	UOsm2MapAssetPlacer* Placer = NewObject<UOsm2MapAssetPlacer>(this);

	// Place individual amenity nodes
	Placer->PlaceAssets(World, RawData, Categories, ProjectedNodes, CoordConverter,
		Dictionary, TerrainGenerator);

	// Place area-based vegetation (forests, parks)
	Placer->PlaceAreaVegetation(World, RawData, Categories, ProjectedNodes, CoordConverter,
		Dictionary, TerrainGenerator);

	return true;
}
