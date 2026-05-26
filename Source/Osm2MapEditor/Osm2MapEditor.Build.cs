using UnrealBuildTool;

public class Osm2MapEditor : ModuleRules
{
	public Osm2MapEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Blutility",
			"Engine",
			"UMG",
			"Osm2Map"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"UnrealEd",
			"Slate",
			"SlateCore",
			"EditorStyle",
			"PropertyEditor",
			"ContentBrowser",
			"LevelEditor",
			"InputCore",
			"ToolMenus",
			"EditorFramework",
			"DesktopPlatform",
			"AppFramework",
			"AssetTools",
			"AssetRegistry",
			"ProceduralMeshComponent",
			"ProceduralMeshComponentEditor",
			"MeshDescription",
			"StaticMeshDescription"
		});
	}
}
