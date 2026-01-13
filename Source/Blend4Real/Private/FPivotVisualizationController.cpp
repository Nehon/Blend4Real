#include "FPivotVisualizationController.h"
#include "Blend4RealUtils.h"
#include "Editor.h"
#include "Selection.h"
#include "EditorViewportClient.h"
#include "SceneView.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Images/SImage.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"

// Pivot marker visual settings - smaller size for cleaner look
static constexpr float PIVOT_MARKER_SIZE = 8.0f;  // Diameter in pixels
static constexpr float PIVOT_MARKER_OUTLINE_WIDTH = 1.0f;

// Colors
static const FLinearColor PIVOT_FILL_COLOR(1.0f, 0.5f, 0.0f, 1.0f);  // Orange
static const FLinearColor PIVOT_OUTLINE_COLOR(0.0f, 0.0f, 0.0f, 1.0f);  // Black

// Static brush instance - needs to persist for the lifetime of the widget
static TOptional<FSlateRoundedBoxBrush> GPivotBrush;

FPivotVisualizationController::FPivotVisualizationController()
{
}

FPivotVisualizationController::~FPivotVisualizationController()
{
	Disable();
}

void FPivotVisualizationController::Enable()
{
	if (bIsEnabled)
	{
		return;
	}

	bIsEnabled = true;

	// Subscribe to selection change events
	SelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw(
		this, &FPivotVisualizationController::OnSelectionChanged);

	// Create overlay widgets
	CreateOverlayWidgets();

	// Initial visualization for current selection
	RefreshVisualization();
}

void FPivotVisualizationController::Disable()
{
	if (!bIsEnabled)
	{
		return;
	}

	bIsEnabled = false;

	// Unsubscribe from selection changes
	if (SelectionChangedHandle.IsValid())
	{
		USelection::SelectionChangedEvent.Remove(SelectionChangedHandle);
		SelectionChangedHandle.Reset();
	}

	DetachFromViewport();
	OverlayCanvas.Reset();
	PivotMarkerImage.Reset();
	PivotMarkerSlot = nullptr;
}

void FPivotVisualizationController::RefreshVisualization()
{
	if (!bIsEnabled)
	{
		return;
	}

	// Check if there's a selection
	if (!GEditor)
	{
		HidePivotMarker();
		return;
	}

	const bool bHasActorSelection = GEditor->GetSelectedActors()->Num() > 0;
	const bool bHasComponentSelection = GEditor->GetSelectedComponents()->Num() > 0;

	if (!bHasActorSelection && !bHasComponentSelection)
	{
		HidePivotMarker();
		return;
	}

	// Compute the pivot point
	const FTransform PivotTransform = Blend4RealUtils::ComputeSelectionPivot();
	CachedPivotWorldLocation = PivotTransform.GetLocation();

	// Ensure we're attached to the current viewport
	AttachToViewport();

	// Update screen position
	UpdatePivotPosition();
}

void FPivotVisualizationController::OnSelectionChanged(UObject* NewSelection)
{
	// Clear custom pivot when selection changes - the pivot should be computed from the new selection
	Blend4RealUtils::ClearCustomPivot();
	RefreshVisualization();
}

void FPivotVisualizationController::CreateOverlayWidgets()
{
	if (OverlayCanvas.IsValid())
	{
		return;
	}

	// Initialize the static brush if needed
	if (!GPivotBrush.IsSet())
	{
		GPivotBrush.Emplace(
			PIVOT_FILL_COLOR,
			PIVOT_MARKER_SIZE / 2.0f,  // Corner radius = half size = circle
			PIVOT_OUTLINE_COLOR,
			PIVOT_MARKER_OUTLINE_WIDTH,
			FVector2f(PIVOT_MARKER_SIZE, PIVOT_MARKER_SIZE)
		);
	}

	// Create the pivot marker image
	SAssignNew(PivotMarkerImage, SImage)
		.Image(&GPivotBrush.GetValue())
		.Visibility(EVisibility::Hidden);  // Start hidden

	// Create a canvas to position the marker absolutely
	SAssignNew(OverlayCanvas, SCanvas)
		.Visibility(EVisibility::HitTestInvisible);  // Don't intercept input

	// Add the slot and capture pointer for later position updates
	// Must capture the slot pointer before the FScopedWidgetSlotArguments destructor runs
	{
		SCanvas::FScopedWidgetSlotArguments SlotArgs = OverlayCanvas->AddSlot();
		SlotArgs.Position(FVector2D::ZeroVector);
		SlotArgs.Size(FVector2D(PIVOT_MARKER_SIZE, PIVOT_MARKER_SIZE));
		SlotArgs.HAlign(HAlign_Center);
		SlotArgs.VAlign(VAlign_Center);
		PivotMarkerSlot = SlotArgs.GetSlot();
		SlotArgs[PivotMarkerImage.ToSharedRef()];
	} // Slot ownership transferred to canvas here, but memory address is still valid

	bMarkerVisible = false;
}

void FPivotVisualizationController::AttachToViewport()
{
	TSharedPtr<SLevelViewport> LevelViewport = GetActiveLevelViewport();
	if (!LevelViewport.IsValid())
	{
		return;
	}

	// Check if we're already attached to this viewport
	if (bAttachedToViewport && CachedLevelViewport.Pin() == LevelViewport)
	{
		return;
	}

	// Detach from old viewport if needed
	DetachFromViewport();

	// Attach to new viewport
	if (OverlayCanvas.IsValid())
	{
		LevelViewport->AddOverlayWidget(OverlayCanvas.ToSharedRef());
		CachedLevelViewport = LevelViewport;
		bAttachedToViewport = true;
	}
}

void FPivotVisualizationController::DetachFromViewport()
{
	if (!bAttachedToViewport)
	{
		return;
	}

	TSharedPtr<SLevelViewport> LevelViewport = CachedLevelViewport.Pin();
	if (LevelViewport.IsValid() && OverlayCanvas.IsValid())
	{
		LevelViewport->RemoveOverlayWidget(OverlayCanvas.ToSharedRef());
	}

	CachedLevelViewport.Reset();
	bAttachedToViewport = false;
}

void FPivotVisualizationController::UpdatePivotPosition()
{
	if (!OverlayCanvas.IsValid() || !PivotMarkerImage.IsValid())
	{
		CreateOverlayWidgets();
		if (!OverlayCanvas.IsValid())
		{
			return;
		}
	}

	// Only update pivot when mouse is over a level viewport
	// This prevents crashes when other viewports (texture editor, etc.) are focused
	if (!FSlateApplication::IsInitialized())
	{
		HidePivotMarker();
		return;
	}
	const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();
	if (!Blend4RealUtils::IsMouseOverViewport(CursorPos, FName("SLevelViewport")))
	{
		HidePivotMarker();
		return;
	}

	// Get the level viewport client
	FVector2D ViewportScreenOrigin;
	FEditorViewportClient* ViewportClient = Blend4RealUtils::GetViewportClientAndScreenOrigin(
		CursorPos, ViewportScreenOrigin, FName("SLevelViewport"));
	if (!ViewportClient)
	{
		HidePivotMarker();
		return;
	}

	// Project world position to viewport-local coordinates
	FVector2D ViewportPosition;
	if (!ProjectWorldToViewport(CachedPivotWorldLocation, ViewportPosition, ViewportClient))
	{
		HidePivotMarker();
		return;
	}

	// Update the canvas slot position
	// The slot uses the position as the anchor point, and we use center alignment
	if (PivotMarkerSlot)
	{
		PivotMarkerSlot->SetPosition(ViewportPosition);
	}

	ShowPivotMarker();
}

void FPivotVisualizationController::HidePivotMarker()
{
	if (bMarkerVisible && PivotMarkerImage.IsValid())
	{
		PivotMarkerImage->SetVisibility(EVisibility::Hidden);
		bMarkerVisible = false;
	}
}

void FPivotVisualizationController::ShowPivotMarker()
{
	if (!bMarkerVisible && PivotMarkerImage.IsValid())
	{
		PivotMarkerImage->SetVisibility(EVisibility::HitTestInvisible);
		bMarkerVisible = true;
	}
}

bool FPivotVisualizationController::ProjectWorldToViewport(const FVector& WorldPosition, FVector2D& OutViewportPosition, FEditorViewportClient* ViewportClient)
{
	if (!ViewportClient || !ViewportClient->Viewport)
	{
		return false;
	}

	// GetScene() can return nullptr for non-level viewports (e.g., texture editor)
	FSceneInterface* Scene = ViewportClient->GetScene();
	if (!Scene)
	{
		return false;
	}

	// Create scene viewI still have acrash when opening a 
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		ViewportClient->Viewport,
		Scene,
		ViewportClient->EngineShowFlags
	));

	FSceneView* SceneView = ViewportClient->CalcSceneView(&ViewFamily);
	if (!SceneView)
	{
		return false;
	}

	// Get viewport size and create rect
	const FIntPoint ViewportSize = ViewportClient->Viewport->GetSizeXY();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return false;
	}
	const FIntRect ViewRect(0, 0, ViewportSize.X, ViewportSize.Y);

	// Project world to screen (viewport-local coordinates)
	const bool bResult = FSceneView::ProjectWorldToScreen(
		WorldPosition,
		ViewRect,
		SceneView->ViewMatrices.GetViewProjectionMatrix(),
		OutViewportPosition
	);

	if (!bResult)
	{
		return false;
	}

	// Check if the point is behind the camera
	const FVector4 ClipSpacePos = SceneView->ViewMatrices.GetViewProjectionMatrix().TransformFVector4(FVector4(WorldPosition, 1.0f));
	if (ClipSpacePos.W <= 0.0f)
	{
		return false;
	}

	// Check if position is within viewport bounds
	if (OutViewportPosition.X < 0 || OutViewportPosition.X > ViewportSize.X ||
		OutViewportPosition.Y < 0 || OutViewportPosition.Y > ViewportSize.Y)
	{
		return false;
	}

	return true;
}

TSharedPtr<SLevelViewport> FPivotVisualizationController::GetActiveLevelViewport()
{
	FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor");
	if (LevelEditorModule)
	{
		return LevelEditorModule->GetFirstActiveLevelViewport();
	}
	return nullptr;
}
