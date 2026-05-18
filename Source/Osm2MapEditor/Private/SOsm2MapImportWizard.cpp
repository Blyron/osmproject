#include "SOsm2MapImportWizard.h"
#include "Pipeline/FOsmImportPipeline.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "Osm2MapImportWizard"

void SOsm2MapImportWizard::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(500.0f)
		.Padding(10.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)

				// Title
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 10)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "Import OpenStreetMap Data"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
				]

				// OSM File
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 5)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OsmFile", "OSM File Path:"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SAssignNew(OsmFilePathText, SEditableTextBox)
						.Text(FText::FromString(Settings.OsmFilePath))
						.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
						{
							Settings.OsmFilePath = Text.ToString();
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(5, 0, 0, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("Browse", "Browse..."))
						.OnClicked(this, &SOsm2MapImportWizard::OnBrowseOsmFile)
					]
				]

				// Elevation Data
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 10, 0, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ElevationDir", "Elevation Data Directory (SRTM .hgt files):"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SAssignNew(ElevationPathText, SEditableTextBox)
						.Text(FText::FromString(Settings.ElevationDataPath))
						.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
						{
							Settings.ElevationDataPath = Text.ToString();
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(5, 0, 0, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("BrowseElev", "Browse..."))
						.OnClicked(this, &SOsm2MapImportWizard::OnBrowseElevation)
					]
				]

				// Import options
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 15, 0, 5)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Options", "Import Options:"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SCheckBox)
					.IsChecked(Settings.bImportTerrain ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
					{
						Settings.bImportTerrain = (State == ECheckBoxState::Checked);
					})
					[
						SNew(STextBlock).Text(LOCTEXT("ImportTerrain", "Import Terrain (Landscape)"))
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SCheckBox)
					.IsChecked(Settings.bImportRoads ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
					{
						Settings.bImportRoads = (State == ECheckBoxState::Checked);
					})
					[
						SNew(STextBlock).Text(LOCTEXT("ImportRoads", "Import Roads"))
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SCheckBox)
					.IsChecked(Settings.bImportBuildings ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
					{
						Settings.bImportBuildings = (State == ECheckBoxState::Checked);
					})
					[
						SNew(STextBlock).Text(LOCTEXT("ImportBuildings", "Import Buildings"))
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SCheckBox)
					.IsChecked(Settings.bImportAssets ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
					{
						Settings.bImportAssets = (State == ECheckBoxState::Checked);
					})
					[
						SNew(STextBlock).Text(LOCTEXT("ImportAssets", "Import Assets (trees, lamps, etc.)"))
					]
				]

				// Building height
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 10, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("DefaultHeight", "Default Building Height (m): "))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SNumericEntryBox<float>)
						.Value_Lambda([this]() { return Settings.DefaultBuildingHeight; })
						.OnValueCommitted_Lambda([this](float Value, ETextCommit::Type)
						{
							Settings.DefaultBuildingHeight = FMath::Clamp(Value, 3.0f, 100.0f);
						})
					]
				]

				// Landscape resolution
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 5, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("LandscapeRes", "Landscape Resolution: "))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SNumericEntryBox<int32>)
						.Value_Lambda([this]() { return Settings.LandscapeResolution; })
						.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type)
						{
							Settings.LandscapeResolution = FMath::Clamp(Value, 127, 8129);
						})
					]
				]

				// Import button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 20, 0, 0)
				.HAlign(HAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("Import", "Import OSM Data"))
					.OnClicked(this, &SOsm2MapImportWizard::OnImport)
					.ContentPadding(FMargin(20, 5))
				]
			]
		]
	];
}

FReply SOsm2MapImportWizard::OnBrowseOsmFile()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OutFiles;
		bool bOpened = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Select OSM File"),
			FPaths::ProjectDir(),
			TEXT(""),
			TEXT("OpenStreetMap Files (*.osm)|*.osm|All Files (*.*)|*.*"),
			EFileDialogFlags::None,
			OutFiles
		);

		if (bOpened && OutFiles.Num() > 0)
		{
			Settings.OsmFilePath = OutFiles[0];
			OsmFilePathText->SetText(FText::FromString(Settings.OsmFilePath));
		}
	}

	return FReply::Handled();
}

FReply SOsm2MapImportWizard::OnBrowseElevation()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		FString OutDir;
		bool bOpened = DesktopPlatform->OpenDirectoryDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Select SRTM Elevation Data Directory"),
			FPaths::ProjectDir(),
			OutDir
		);

		if (bOpened)
		{
			Settings.ElevationDataPath = OutDir;
			ElevationPathText->SetText(FText::FromString(Settings.ElevationDataPath));
		}
	}

	return FReply::Handled();
}

FReply SOsm2MapImportWizard::OnImport()
{
	if (Settings.OsmFilePath.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoFile", "Please select an OSM file first."));
		return FReply::Handled();
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoWorld", "No editor world available."));
		return FReply::Handled();
	}

	UOsmImportPipeline* Pipeline = NewObject<UOsmImportPipeline>();
	bool bSuccess = Pipeline->Execute(World, Settings);

	if (bSuccess)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("Success", "OSM import completed successfully!"));
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("Failed", "OSM import failed. Check the Output Log for details."));
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
