#include "FSelectionActionsController.h"
#include "FTransformController.h"
#include "Blend4RealUtils.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Engine/Selection.h"

FSelectionActionsController::FSelectionActionsController(TSharedPtr<FTransformController> InTransformController)
	: TransformController(InTransformController)
{
}

void FSelectionActionsController::DuplicateSelectedAndGrab() const
{
	if (!GEditor || !GUnrealEd)
	{
		return;
	}

	// Selection actions only work in Level Editor viewport
	if (!Blend4RealUtils::IsLevelEditorViewportFocused())
	{
		return;
	}

	const USelection* SelectedActors = GEditor->GetSelectedActors();
	if (!SelectedActors || SelectedActors->Num() == 0)
	{
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return;
	}

	// Use Unreal's built-in duplication which:
	// 1. Duplicates selected actors
	// 2. Automatically selects the new duplicates
	// 3. Handles undo/redo properly
	// bOffsetLocations = false so duplicates are created at the same position
	GUnrealEd->edactDuplicateSelected(World->GetCurrentLevel(), false);

	// Now the new actors are selected, enter translation mode immediately
	TSharedPtr<FTransformController> TransformCtrl = TransformController.Pin();
	if (TransformCtrl.IsValid())
	{
		TransformCtrl->BeginTransform(ETransformMode::Translation);
	}
}

void FSelectionActionsController::DeleteSelected()
{
	if (!GEditor || !GUnrealEd)
	{
		return;
	}

	// Selection actions only work in Level Editor viewport
	if (!Blend4RealUtils::IsLevelEditorViewportFocused())
	{
		return;
	}

	const USelection* SelectedActors = GEditor->GetSelectedActors();
	if (!SelectedActors || SelectedActors->Num() == 0)
	{
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return;
	}

	GEditor->BeginTransaction(TEXT(""), FText::FromString("Remove Selected Actors"), nullptr);
	Blend4RealUtils::MarkSelectionModified();
	GUnrealEd->edactDeleteSelected(World);
	GEditor->EndTransaction();
}
