using UnrealBuildTool;
using System.IO;

public class Osm2Map : ModuleRules
{
	public Osm2Map(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bEnableExceptions = true;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"ProceduralMeshComponent",
			"Landscape",
			"Foliage",
			"MeshDescription",
			"StaticMeshDescription",
			"GeometryCore",
			"MeshConversion",
			"RenderCore"
		});

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.Add("LandscapeEditor");
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate",
			"SlateCore"
		});

		// --- ThirdParty: osm2xodr + pugixml ---
		// ModuleDirectory = Source/Osm2Map/
		// Project root = Source/Osm2Map/../../ = project root
		string ProjectRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
		string Osm2XodrSrc = Path.Combine(ProjectRoot, "ThirdParty", "osm2xodr", "src");
		string PugixmlSrc  = Path.Combine(ProjectRoot, "ThirdParty", "osm2xodr", "build", "_deps", "pugixml-src", "src");

		// Log for debugging (visible in UBT output)
		System.Console.WriteLine("Osm2Map: Osm2XodrSrc = " + Osm2XodrSrc);
		System.Console.WriteLine("Osm2Map: Exists = " + Directory.Exists(Osm2XodrSrc));

		PublicIncludePaths.AddRange(new string[] {
			Osm2XodrSrc,
			Path.Combine(Osm2XodrSrc, "osm"),
			Path.Combine(Osm2XodrSrc, "conversion"),
			Path.Combine(Osm2XodrSrc, "xodr"),
			PugixmlSrc
		});

		PrivateDefinitions.Add("PUGIXML_NO_XPATH");
	}
}
