#pragma once

#include "CoreMinimal.h"

/**
 * Loads and interpolates SRTM elevation data (.hgt files).
 * SRTM1 = 1 arc-second (~30m resolution), 3601x3601 int16 big-endian grid.
 * Filename encodes southwest corner: e.g. N43W003.hgt
 */
class OSM2MAP_API FElevationDataProvider
{
public:
	/** Load elevation data covering the given bounding box from a directory of .hgt files */
	bool LoadFromDirectory(const FString& Directory, double MinLat, double MaxLat, double MinLon, double MaxLon);

	/** Sample elevation at a lat/lon point using bilinear interpolation. Returns meters above WGS84. */
	float SampleElevation(double Lat, double Lon) const;

	/** Check if elevation data is available */
	bool IsLoaded() const { return Tiles.Num() > 0; }

	/** Get the min/max elevation values in the loaded data */
	float GetMinElevation() const { return MinElevation; }
	float GetMaxElevation() const { return MaxElevation; }

	/**
	 * Generate a heightmap array suitable for UE Landscape.
	 * Output is a TArray<uint16> where 32768 = 0m relative offset, scaled to fit the elevation range.
	 */
	TArray<uint16> GenerateHeightmap(int32 Resolution, double MinLat, double MaxLat, double MinLon, double MaxLon) const;

private:
	/** Load a single .hgt tile */
	bool LoadHgtFile(const FString& FilePath, int32 TileLat, int32 TileLon);

	/** Get SRTM filename for a tile (e.g., "N43W003.hgt") */
	static FString GetHgtFilename(int32 Lat, int32 Lon);

	struct FElevationTile
	{
		TArray<int16> Data;  // 3601*3601 or 1201*1201
		int32 Size = 3601;   // Samples per side
		int32 BaseLat = 0;   // Southwest corner latitude (integer)
		int32 BaseLon = 0;   // Southwest corner longitude (integer)
	};

	TArray<FElevationTile> Tiles;
	float MinElevation = 0.0f;
	float MaxElevation = 0.0f;

	const FElevationTile* FindTile(int32 Lat, int32 Lon) const;
};
