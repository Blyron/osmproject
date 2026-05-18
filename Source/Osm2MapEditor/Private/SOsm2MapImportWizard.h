#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Pipeline/FOsmImportSettings.h"

/**
 * Slate widget for the OSM import wizard.
 * Provides a UI to configure and execute the OSM import pipeline.
 */
class SOsm2MapImportWizard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOsm2MapImportWizard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** Browse for OSM file */
	FReply OnBrowseOsmFile();

	/** Browse for elevation data directory */
	FReply OnBrowseElevation();

	/** Execute import */
	FReply OnImport();

	/** Current import settings */
	FOsmImportSettings Settings;

	/** UI text blocks */
	TSharedPtr<SEditableTextBox> OsmFilePathText;
	TSharedPtr<SEditableTextBox> ElevationPathText;
};
