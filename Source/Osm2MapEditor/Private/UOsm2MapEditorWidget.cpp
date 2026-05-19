#include "UOsm2MapEditorWidget.h"

#include "Assets/UOsmAssetDictionary.h"
#include "Editor.h"
#include "Pipeline/FOsmImportPipeline.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "ProceduralMeshConversion.h"
#include "MeshDescription.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsm2MapEditor, Log, All);

namespace
{
	// Collects ProceduralMesh data from every actor tagged with `Tag`, merges all sections
	// into one mesh, and saves it as a UStaticMesh asset at ContentFolderPath/AssetName.
	void SaveMergedMeshAsset(
		UWorld* World,
		const FName Tag,
		const FString& ContentFolderPath,
		const FString& AssetName)
	{
		TArray<FVector>    AllVerts;
		TArray<int32>     AllTris;
		TArray<FVector>    AllNormals;
		TArray<FVector2D>  AllUVs;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->Tags.Contains(Tag)) continue;

			TArray<UProceduralMeshComponent*> ProcMeshes;
			It->GetComponents<UProceduralMeshComponent>(ProcMeshes);

			for (UProceduralMeshComponent* ProcMesh : ProcMeshes)
			{
				for (int32 SectIdx = 0; SectIdx < ProcMesh->GetNumSections(); ++SectIdx)
				{
					FProcMeshSection* Section = ProcMesh->GetProcMeshSection(SectIdx);
					if (!Section || Section->ProcVertexBuffer.Num() == 0) continue;

					const int32 BaseVert = AllVerts.Num();
					for (const FProcMeshVertex& V : Section->ProcVertexBuffer)
					{
						AllVerts.Add(V.Position);
						AllNormals.Add(V.Normal);
						AllUVs.Add(V.UV0);
					}
					for (uint32 Idx : Section->ProcIndexBuffer)
					{
						AllTris.Add(static_cast<int32>(Idx) + BaseVert);
					}
				}
			}
		}

		if (AllVerts.Num() == 0 || AllTris.Num() == 0)
		{
			UE_LOG(LogOsm2MapEditor, Log, TEXT("No mesh data for tag '%s', skipping save"), *Tag.ToString());
			return;
		}

		// Bridge through a transient ProceduralMeshComponent so BuildMeshDescription
		// handles all the attribute / material-slot setup automatically.
		UProceduralMeshComponent* TempMesh =
			NewObject<UProceduralMeshComponent>(GetTransientPackage());
		TempMesh->CreateMeshSection(0, AllVerts, AllTris, AllNormals, AllUVs,
			TArray<FColor>(), TArray<FProcMeshTangent>(), /*bCreateCollision=*/false);

		FMeshDescription MeshDesc = BuildMeshDescription(TempMesh);
		TempMesh->MarkAsGarbage();

		if (MeshDesc.Vertices().Num() == 0) return;

		// Create the static mesh package
		const FString FullPackageName = ContentFolderPath + TEXT("/") + AssetName;
		UPackage* Package = CreatePackage(*FullPackageName);
		Package->FullyLoad();

		UStaticMesh* SM = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone);
		SM->InitResources();
		SM->GetStaticMaterials().Add(FStaticMaterial());

		FStaticMeshSourceModel& SrcModel = SM->AddSourceModel();
		SrcModel.BuildSettings.bRecomputeNormals          = false;
		SrcModel.BuildSettings.bRecomputeTangents         = true;
		SrcModel.BuildSettings.bRemoveDegenerates         = false;
		SrcModel.BuildSettings.bGenerateLightmapUVs       = true;
		SrcModel.BuildSettings.SrcLightmapIndex           = 0;
		SrcModel.BuildSettings.DstLightmapIndex           = 1;
		SrcModel.BuildSettings.bUseHighPrecisionTangentBasis = false;
		SrcModel.BuildSettings.bUseFullPrecisionUVs       = false;

		if (FMeshDescription* MDPtr = SM->CreateMeshDescription(0))
		{
			*MDPtr = MeshDesc;
			SM->CommitMeshDescription(0);
		}

		SM->Build(false);
		SM->PostEditChange();

		FAssetRegistryModule::AssetCreated(SM);
		Package->MarkPackageDirty();

		const FString FileName = FPackageName::LongPackageNameToFilename(
			FullPackageName, FPackageName::GetAssetPackageExtension());
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(
			*FPaths::GetPath(FileName));

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, SM, *FileName, SaveArgs);

		UE_LOG(LogOsm2MapEditor, Log, TEXT("Saved static mesh '%s' (%d verts, %d tris)"),
			*FullPackageName, AllVerts.Num(), AllTris.Num() / 3);
	}
}(const FOsmImportSettings& InSettings)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		return false;
	}

	// Mutable copy so we can assign a new asset dictionary if needed
	FOsmImportSettings Settings = InSettings;

	// Create a dated folder + AssetDictionary data asset before running the import.
	// ContentFolderPath is hoisted so it's accessible for mesh saving below.
	FString ContentFolderPath;
	{
		const FString OsmBaseName = FPaths::GetBaseFilename(Settings.OsmFilePath);
		const FString DateStr = FDateTime::Now().ToString(TEXT("%Y-%m-%d"));
		const FString FolderName = FString::Printf(TEXT("%s_%s"), *DateStr, *OsmBaseName);
		ContentFolderPath = TEXT("/Game/Osm/") + FolderName;

		// Create the directory on disk
		const FString DiskPath = FPaths::ProjectContentDir() / TEXT("Osm") / FolderName;
		FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*DiskPath);

		// Register with the asset registry
		FAssetRegistryModule::GetRegistry().AddPath(ContentFolderPath);

		// Create the UOsmAssetDictionary data asset
		FAssetToolsModule& AssetToolsModule =
			FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		UObject* NewAsset = AssetToolsModule.Get().CreateAsset(
			TEXT("AssetDictionary"), ContentFolderPath,
			UOsmAssetDictionary::StaticClass(), nullptr);

		if (UOsmAssetDictionary* Dict = Cast<UOsmAssetDictionary>(NewAsset))
		{
			Dict->PopulateDefaults();
			Dict->MarkPackageDirty();

			// Use the new dictionary if the caller didn't provide one
			if (Settings.AssetDictionary.IsNull())
			{
				Settings.AssetDictionary = Dict;
			}
		}
	}

	UOsmImportPipeline* Pipeline = UOsmImportPipeline::NewPipeline();
	const bool bSuccess = Pipeline ? Pipeline->Execute(EditorWorld, Settings) : false;

	// After a successful import, save the generated geometry as static mesh assets
	// in the same content folder so they can be opened and inspected in the editor.
	if (bSuccess && !ContentFolderPath.IsEmpty())
	{
		SaveMergedMeshAsset(EditorWorld, FName("Osm2Map_Building"), ContentFolderPath, TEXT("OSM_Buildings"));
		SaveMergedMeshAsset(EditorWorld, FName("Osm2Map_Road"),     ContentFolderPath, TEXT("OSM_Roads"));
		SaveMergedMeshAsset(EditorWorld, FName("Osm2Map_Junction"), ContentFolderPath, TEXT("OSM_Junctions"));
		SaveMergedMeshAsset(EditorWorld, FName("Osm2Map_Terrain"),  ContentFolderPath, TEXT("OSM_Terrain"));
	}

	return bSuccess;
}

bool UOsm2MapEditorWidget::ExportDictionaryToJson(UOsmAssetDictionary* Dictionary, const FString& FilePath)
{
	return Dictionary ? Dictionary->ExportToJson(FilePath) : false;
}

bool UOsm2MapEditorWidget::ImportDictionaryFromJson(UOsmAssetDictionary* Dictionary, const FString& FilePath)
{
	return Dictionary ? Dictionary->ImportFromJson(FilePath) : false;
}