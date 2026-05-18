#include "Osm2MapEditor.h"
#include "SOsm2MapImportWizard.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"

#define LOCTEXT_NAMESPACE "FOsm2MapEditorModule"

static const FName Osm2MapImportTabName("Osm2MapImport");

void FOsm2MapEditorModule::StartupModule()
{
	// Register the import wizard tab
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(Osm2MapImportTabName,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&) -> TSharedRef<SDockTab>
		{
			return SNew(SDockTab)
				.TabRole(ETabRole::NomadTab)
				.Label(LOCTEXT("ImportTabTitle", "OSM Import"))
				[
					SNew(SOsm2MapImportWizard)
				];
		}))
		.SetDisplayName(LOCTEXT("ImportTabDisplayName", "OSM Import"))
		.SetTooltipText(LOCTEXT("ImportTabTooltip", "Import OpenStreetMap data to generate terrain, roads, buildings and assets"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	// Register toolbar extension when menus are ready
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		if (ToolbarMenu)
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("Osm2Map");
			Section.AddMenuEntry(
				"Osm2MapImport",
				LOCTEXT("ImportOSM", "Import OSM"),
				LOCTEXT("ImportOSMTooltip", "Import OpenStreetMap data to generate terrain, roads, buildings and assets"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"),
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					FGlobalTabmanager::Get()->TryInvokeTab(Osm2MapImportTabName);
				}))
			);
		}
	}));
}

void FOsm2MapEditorModule::ShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(Osm2MapImportTabName);
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FOsm2MapEditorModule, Osm2MapEditor)
