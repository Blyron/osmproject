#include "Coord/FOsmCoordinateConverter.h"
#include "conversion/Projection.h"

FOsmCoordinateConverter::FOsmCoordinateConverter(float InScale)
	: Scale(InScale)
{
}

FVector2D FOsmCoordinateConverter::OsmToUE2D(double OsmX, double OsmY) const
{
	// osm2xodr: X=east, Y=north (meters)
	// UE: X=forward(north), Y=right(east) (centimeters)
	return FVector2D(OsmY * Scale, OsmX * Scale);
}

FVector FOsmCoordinateConverter::OsmToUE(double OsmX, double OsmY, double ElevationMeters) const
{
	return FVector(OsmY * Scale, OsmX * Scale, ElevationMeters * Scale);
}

FVector FOsmCoordinateConverter::OsmToUE(const XY& OsmPos, double ElevationMeters) const
{
	return OsmToUE(OsmPos.x, OsmPos.y, ElevationMeters);
}

void FOsmCoordinateConverter::UEToOsm(const FVector& UEPos, double& OutOsmX, double& OutOsmY) const
{
	// Reverse: UE X(north) -> osm Y, UE Y(east) -> osm X
	OutOsmX = UEPos.Y / Scale;
	OutOsmY = UEPos.X / Scale;
}

float FOsmCoordinateConverter::OsmHeadingToUEYaw(double HeadingRadians) const
{
	// osm2xodr heading: 0=east, CCW positive (math convention, radians)
	// UE yaw: 0=forward(north), CW positive (degrees)
	// Conversion: UE_yaw = 90 - heading_degrees
	double HeadingDegrees = FMath::RadiansToDegrees(HeadingRadians);
	float UEYaw = 90.0f - static_cast<float>(HeadingDegrees);
	return FRotator::NormalizeAxis(UEYaw);
}
