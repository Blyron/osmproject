#pragma once

#include "CoreMinimal.h"

// Forward declare osm2xodr types
struct XY;

/**
 * Centralized coordinate conversion between osm2xodr local meters and UE world coordinates.
 *
 * osm2xodr coordinate system: X = east, Y = north, meters, origin at bbox center
 * UE coordinate system: X = forward (north), Y = right (east), Z = up, centimeters, left-handed
 *
 * Conversion:
 *   UE_X = osm_Y * Scale   (north -> forward)
 *   UE_Y = osm_X * Scale   (east -> right)
 *   UE_Z = elevation * Scale
 */
class OSM2MAP_API FOsmCoordinateConverter
{
public:
	FOsmCoordinateConverter() = default;
	explicit FOsmCoordinateConverter(float InScale);

	/** Convert osm2xodr local XY (meters) to UE world XY (cm) */
	FVector2D OsmToUE2D(double OsmX, double OsmY) const;

	/** Convert osm2xodr local XY to UE world position with elevation */
	FVector OsmToUE(double OsmX, double OsmY, double ElevationMeters = 0.0) const;

	/** Convert osm2xodr XY struct to UE world position */
	FVector OsmToUE(const XY& OsmPos, double ElevationMeters = 0.0) const;

	/** Convert UE world position back to osm2xodr local meters */
	void UEToOsm(const FVector& UEPos, double& OutOsmX, double& OutOsmY) const;

	/** Convert heading from osm2xodr (radians, 0=east, CCW) to UE rotation (degrees) */
	float OsmHeadingToUEYaw(double HeadingRadians) const;

	float GetScale() const { return Scale; }

private:
	float Scale = 100.0f; // meters to centimeters
};
