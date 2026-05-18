#include "Buildings/FBuildingExtractor.h"

DEFINE_LOG_CATEGORY_STATIC(LogBuildingExtract, Log, All);

TArray<FBuildingFootprint> FBuildingExtractor::Extract(
	const FOsmRawData& RawData,
	const FOsmCategories& Categories,
	const TMap<int64, FVector2D>& ProjectedNodes,
	float DefaultHeight)
{
	TArray<FBuildingFootprint> Results;

	// Extract simple buildings from ways
	for (int64 WayId : Categories.BuildingWayIds)
	{
		const FOsmWayData* Way = RawData.Ways.Find(WayId);
		if (!Way || Way->NodeRefs.Num() < 4) continue;

		// Check if this way is part of a multipolygon relation (skip if so - handled by relation)
		// Simple heuristic: if the way itself has building=* tag, it's standalone
		FBuildingFootprint Footprint = ExtractFromWay(*Way, ProjectedNodes, DefaultHeight);
		if (Footprint.OuterRing.Num() >= 3)
		{
			Results.Add(MoveTemp(Footprint));
		}
	}

	// Extract complex buildings from multipolygon relations
	for (int64 RelId : Categories.MultipolygonRelationIds)
	{
		const FOsmRelationData* Rel = RawData.Relations.Find(RelId);
		if (!Rel) continue;

		// Only process building multipolygons
		const FString* BuildingTag = Rel->Tags.Find(TEXT("building"));
		if (!BuildingTag) continue;

		TArray<FBuildingFootprint> RelBuildings = ExtractFromRelation(*Rel, RawData, ProjectedNodes, DefaultHeight);
		Results.Append(MoveTemp(RelBuildings));
	}

	UE_LOG(LogBuildingExtract, Log, TEXT("Extracted %d building footprints"), Results.Num());
	return Results;
}

FBuildingFootprint FBuildingExtractor::ExtractFromWay(
	const FOsmWayData& Way,
	const TMap<int64, FVector2D>& ProjectedNodes,
	float DefaultHeight)
{
	FBuildingFootprint Footprint;
	Footprint.OsmId = Way.Id;
	Footprint.Tags = Way.Tags;
	Footprint.BuildingType = Way.GetTag(TEXT("building"));
	Footprint.RoofShape = Way.GetTag(TEXT("roof:shape"));
	if (Footprint.RoofShape.IsEmpty()) Footprint.RoofShape = TEXT("flat");

	// Build outer ring from node references
	for (int64 NodeRef : Way.NodeRefs)
	{
		const FVector2D* Pos = ProjectedNodes.Find(NodeRef);
		if (Pos)
		{
			Footprint.OuterRing.Add(*Pos);
		}
	}

	// Remove duplicate closing vertex if present
	if (Footprint.OuterRing.Num() >= 2 &&
		FVector2D::Distance(Footprint.OuterRing[0], Footprint.OuterRing.Last()) < 0.01)
	{
		Footprint.OuterRing.Pop();
	}

	if (Footprint.OuterRing.Num() < 3)
	{
		return Footprint;
	}

	// Ensure counter-clockwise winding for outer ring
	EnsureCCW(Footprint.OuterRing);

	// Determine height
	Footprint.Height = DetermineHeight(Way.Tags, DefaultHeight);

	// Check complexity
	Footprint.bIsComplex = !IsConvex(Footprint.OuterRing);

	return Footprint;
}

TArray<FBuildingFootprint> FBuildingExtractor::ExtractFromRelation(
	const FOsmRelationData& Relation,
	const FOsmRawData& RawData,
	const TMap<int64, FVector2D>& ProjectedNodes,
	float DefaultHeight)
{
	TArray<FBuildingFootprint> Results;

	// Collect outer and inner rings
	TArray<TArray<FVector2D>> OuterRings;
	TArray<TArray<FVector2D>> InnerRings;

	for (const FOsmRelationMember& Member : Relation.Members)
	{
		if (Member.Type != TEXT("way")) continue;

		const FOsmWayData* Way = RawData.Ways.Find(Member.Ref);
		if (!Way) continue;

		TArray<FVector2D> Ring;
		for (int64 NodeRef : Way->NodeRefs)
		{
			const FVector2D* Pos = ProjectedNodes.Find(NodeRef);
			if (Pos) Ring.Add(*Pos);
		}

		// Remove duplicate closing vertex
		if (Ring.Num() >= 2 && FVector2D::Distance(Ring[0], Ring.Last()) < 0.01)
		{
			Ring.Pop();
		}

		if (Ring.Num() < 3) continue;

		if (Member.Role == TEXT("outer"))
		{
			EnsureCCW(Ring);
			OuterRings.Add(MoveTemp(Ring));
		}
		else if (Member.Role == TEXT("inner"))
		{
			// Inner rings should be clockwise (opposite of outer)
			// We store them as-is; the mesh generator will handle winding
			InnerRings.Add(MoveTemp(Ring));
		}
	}

	// Create a footprint for each outer ring
	for (TArray<FVector2D>& Outer : OuterRings)
	{
		FBuildingFootprint Footprint;
		Footprint.OsmId = Relation.Id;
		Footprint.Tags = Relation.Tags;
		Footprint.BuildingType = Relation.Tags.FindRef(TEXT("building"));
		Footprint.RoofShape = Relation.Tags.FindRef(TEXT("roof:shape"));
		if (Footprint.RoofShape.IsEmpty()) Footprint.RoofShape = TEXT("flat");
		Footprint.OuterRing = MoveTemp(Outer);
		Footprint.Height = DetermineHeight(Relation.Tags, DefaultHeight);

		// Find inner rings that fall within this outer ring
		// Simple check: test if first point of inner ring is inside outer ring
		for (const TArray<FVector2D>& Inner : InnerRings)
		{
			if (Inner.Num() > 0)
			{
				// Simple containment check using the first point
				bool bInside = false;
				const FVector2D& TestPoint = Inner[0];
				int32 N = Footprint.OuterRing.Num();
				for (int32 i = 0, j = N - 1; i < N; j = i++)
				{
					const FVector2D& Pi = Footprint.OuterRing[i];
					const FVector2D& Pj = Footprint.OuterRing[j];
					if (((Pi.Y > TestPoint.Y) != (Pj.Y > TestPoint.Y)) &&
						(TestPoint.X < (Pj.X - Pi.X) * (TestPoint.Y - Pi.Y) / (Pj.Y - Pi.Y) + Pi.X))
					{
						bInside = !bInside;
					}
				}
				if (bInside)
				{
					Footprint.Holes.Add(Inner);
				}
			}
		}

		Footprint.bIsComplex = true; // Multipolygon buildings are always complex
		Results.Add(MoveTemp(Footprint));
	}

	return Results;
}

float FBuildingExtractor::DetermineHeight(const TMap<FString, FString>& Tags, float DefaultHeight)
{
	// Priority 1: building:height tag (meters)
	const FString* BuildingHeight = Tags.Find(TEXT("building:height"));
	if (BuildingHeight && !BuildingHeight->IsEmpty())
	{
		float H = FCString::Atof(**BuildingHeight);
		if (H > 0.0f) return H;
	}

	// Priority 2: height tag
	const FString* Height = Tags.Find(TEXT("height"));
	if (Height && !Height->IsEmpty())
	{
		float H = FCString::Atof(**Height);
		if (H > 0.0f) return H;
	}

	// Priority 3: building:levels * 3m per level
	const FString* Levels = Tags.Find(TEXT("building:levels"));
	if (Levels && !Levels->IsEmpty())
	{
		int32 L = FCString::Atoi(**Levels);
		if (L > 0) return L * 3.0f;
	}

	// Priority 4: default by building type
	const FString* Type = Tags.Find(TEXT("building"));
	if (Type)
	{
		if (*Type == TEXT("house") || *Type == TEXT("detached") || *Type == TEXT("semidetached_house"))
			return 7.0f;
		if (*Type == TEXT("apartments") || *Type == TEXT("residential"))
			return 15.0f;
		if (*Type == TEXT("commercial") || *Type == TEXT("office"))
			return 12.0f;
		if (*Type == TEXT("church") || *Type == TEXT("cathedral"))
			return 20.0f;
		if (*Type == TEXT("industrial") || *Type == TEXT("warehouse"))
			return 8.0f;
	}

	return DefaultHeight;
}

bool FBuildingExtractor::IsConvex(const TArray<FVector2D>& Polygon)
{
	int32 N = Polygon.Num();
	if (N < 3) return false;

	bool bPositive = false;
	bool bNegative = false;

	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D& A = Polygon[i];
		const FVector2D& B = Polygon[(i + 1) % N];
		const FVector2D& C = Polygon[(i + 2) % N];

		double Cross = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
		if (Cross > 0) bPositive = true;
		if (Cross < 0) bNegative = true;

		if (bPositive && bNegative) return false;
	}

	return true;
}

void FBuildingExtractor::EnsureCCW(TArray<FVector2D>& Polygon)
{
	// Compute signed area (shoelace formula)
	double Area = 0.0;
	int32 N = Polygon.Num();
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D& A = Polygon[i];
		const FVector2D& B = Polygon[(i + 1) % N];
		Area += (B.X - A.X) * (B.Y + A.Y);
	}

	// If area is positive, winding is clockwise -> reverse
	if (Area > 0)
	{
		Algo::Reverse(Polygon);
	}
}
