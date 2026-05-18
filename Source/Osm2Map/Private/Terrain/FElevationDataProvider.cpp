#include "Terrain/FElevationDataProvider.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElevation, Log, All);

bool FElevationDataProvider::LoadFromDirectory(const FString& Directory, double MinLat, double MaxLat, double MinLon, double MaxLon)
{
	Tiles.Empty();
	MinElevation = TNumericLimits<float>::Max();
	MaxElevation = TNumericLimits<float>::Lowest();

	// Determine which SRTM tiles we need (tiles are 1x1 degree, named by SW corner)
	int32 LatStart = FMath::FloorToInt32(MinLat);
	int32 LatEnd = FMath::FloorToInt32(MaxLat);
	int32 LonStart = FMath::FloorToInt32(MinLon);
	int32 LonEnd = FMath::FloorToInt32(MaxLon);

	bool bAnyLoaded = false;
	for (int32 Lat = LatStart; Lat <= LatEnd; ++Lat)
	{
		for (int32 Lon = LonStart; Lon <= LonEnd; ++Lon)
		{
			FString Filename = GetHgtFilename(Lat, Lon);
			FString FilePath = FPaths::Combine(Directory, Filename);

			if (FPaths::FileExists(FilePath))
			{
				if (LoadHgtFile(FilePath, Lat, Lon))
				{
					bAnyLoaded = true;
					UE_LOG(LogElevation, Log, TEXT("Loaded SRTM tile: %s"), *Filename);
				}
			}
			else
			{
				UE_LOG(LogElevation, Warning, TEXT("SRTM tile not found: %s"), *FilePath);
			}
		}
	}

	if (bAnyLoaded)
	{
		UE_LOG(LogElevation, Log, TEXT("Elevation range: %.1fm to %.1fm"), MinElevation, MaxElevation);
	}

	return bAnyLoaded;
}

bool FElevationDataProvider::LoadHgtFile(const FString& FilePath, int32 TileLat, int32 TileLon)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		UE_LOG(LogElevation, Error, TEXT("Failed to load file: %s"), *FilePath);
		return false;
	}

	// Determine tile size from file size
	int32 TileSize;
	if (FileData.Num() == 3601 * 3601 * 2)
	{
		TileSize = 3601; // SRTM1 (1 arc-second)
	}
	else if (FileData.Num() == 1201 * 1201 * 2)
	{
		TileSize = 1201; // SRTM3 (3 arc-second)
	}
	else
	{
		UE_LOG(LogElevation, Error, TEXT("Invalid HGT file size: %d bytes"), FileData.Num());
		return false;
	}

	FElevationTile& Tile = Tiles.AddDefaulted_GetRef();
	Tile.Size = TileSize;
	Tile.BaseLat = TileLat;
	Tile.BaseLon = TileLon;
	Tile.Data.SetNum(TileSize * TileSize);

	// HGT files are big-endian int16
	const uint8* Ptr = FileData.GetData();
	for (int32 i = 0; i < TileSize * TileSize; ++i)
	{
		int16 Value = static_cast<int16>((Ptr[i * 2] << 8) | Ptr[i * 2 + 1]);
		Tile.Data[i] = Value;

		// Track min/max, ignoring void values (-32768)
		if (Value != -32768)
		{
			MinElevation = FMath::Min(MinElevation, static_cast<float>(Value));
			MaxElevation = FMath::Max(MaxElevation, static_cast<float>(Value));
		}
	}

	return true;
}

FString FElevationDataProvider::GetHgtFilename(int32 Lat, int32 Lon)
{
	TCHAR LatChar = Lat >= 0 ? TEXT('N') : TEXT('S');
	TCHAR LonChar = Lon >= 0 ? TEXT('E') : TEXT('W');
	return FString::Printf(TEXT("%c%02d%c%03d.hgt"), LatChar, FMath::Abs(Lat), LonChar, FMath::Abs(Lon));
}

const FElevationDataProvider::FElevationTile* FElevationDataProvider::FindTile(int32 Lat, int32 Lon) const
{
	for (const FElevationTile& Tile : Tiles)
	{
		if (Tile.BaseLat == Lat && Tile.BaseLon == Lon)
		{
			return &Tile;
		}
	}
	return nullptr;
}

float FElevationDataProvider::SampleElevation(double Lat, double Lon) const
{
	int32 TileLat = FMath::FloorToInt32(Lat);
	int32 TileLon = FMath::FloorToInt32(Lon);

	const FElevationTile* Tile = FindTile(TileLat, TileLon);
	if (!Tile)
	{
		return 0.0f;
	}

	// Position within tile (0..1)
	double FracLat = Lat - TileLat;
	double FracLon = Lon - TileLon;

	// Convert to sample indices (note: rows go from north to south in HGT format)
	double RowF = (1.0 - FracLat) * (Tile->Size - 1);
	double ColF = FracLon * (Tile->Size - 1);

	int32 Row0 = FMath::Clamp(FMath::FloorToInt32(RowF), 0, Tile->Size - 2);
	int32 Col0 = FMath::Clamp(FMath::FloorToInt32(ColF), 0, Tile->Size - 2);
	int32 Row1 = Row0 + 1;
	int32 Col1 = Col0 + 1;

	double FracRow = RowF - Row0;
	double FracCol = ColF - Col0;

	// Bilinear interpolation
	auto GetSample = [&](int32 Row, int32 Col) -> float
	{
		int16 Value = Tile->Data[Row * Tile->Size + Col];
		if (Value == -32768) return 0.0f; // Void
		return static_cast<float>(Value);
	};

	float V00 = GetSample(Row0, Col0);
	float V10 = GetSample(Row1, Col0);
	float V01 = GetSample(Row0, Col1);
	float V11 = GetSample(Row1, Col1);

	float Result = FMath::Lerp(
		FMath::Lerp(V00, V01, FracCol),
		FMath::Lerp(V10, V11, FracCol),
		FracRow
	);

	return Result;
}

TArray<uint16> FElevationDataProvider::GenerateHeightmap(int32 Resolution, double MinLat, double MaxLat, double MinLon, double MaxLon) const
{
	TArray<uint16> Heightmap;
	Heightmap.SetNum(Resolution * Resolution);

	if (!IsLoaded())
	{
		// Flat terrain at mid-height
		for (int32 i = 0; i < Resolution * Resolution; ++i)
		{
			Heightmap[i] = 32768;
		}
		return Heightmap;
	}

	float ElevRange = MaxElevation - MinElevation;
	if (ElevRange < 1.0f) ElevRange = 1.0f;

	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		double Lat = MaxLat - (MaxLat - MinLat) * Y / (Resolution - 1); // North to south
		for (int32 X = 0; X < Resolution; ++X)
		{
			double Lon = MinLon + (MaxLon - MinLon) * X / (Resolution - 1);
			float Elev = SampleElevation(Lat, Lon);

			// Map to uint16 range with 32768 as the midpoint (0m offset)
			// Scale so the full elevation range fits within a reasonable portion of uint16
			float Normalized = (Elev - MinElevation) / ElevRange;
			uint16 HeightValue = static_cast<uint16>(FMath::Clamp(Normalized * 65535.0f, 0.0f, 65535.0f));
			Heightmap[Y * Resolution + X] = HeightValue;
		}
	}

	return Heightmap;
}
