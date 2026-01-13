#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCanvas.h"

class SLevelViewport;
class SImage;
class FEditorViewportClient;

/**
 * Renders the pivot point of the current selection as a visual marker.
 * Uses a viewport overlay to display an orange disc with black outline at the pivot location.
 * The marker maintains constant screen size regardless of camera distance.
 */
class FPivotVisualizationController
{
public:
	FPivotVisualizationController();
	~FPivotVisualizationController();

	/** Enable pivot visualization and start listening to selection changes */
	void Enable();

	/** Disable pivot visualization and stop listening to selection changes */
	void Disable();

	/** Returns true if visualization is currently enabled */
	bool IsEnabled() const { return bIsEnabled; }

	/** Force refresh of the pivot visualization (call when camera moves or transforms happen) */
	void RefreshVisualization();

private:
	/** Called when selection changes in the editor */
	void OnSelectionChanged(UObject* NewSelection);

	/** Add the overlay widget to the current level viewport */
	void AttachToViewport();

	/** Remove the overlay widget from the viewport */
	void DetachFromViewport();

	/** Create the overlay widgets */
	void CreateOverlayWidgets();

	/** Update the pivot point visualization position */
	void UpdatePivotPosition();

	/** Hide the pivot marker (when no selection) */
	void HidePivotMarker();

	/** Show the pivot marker */
	void ShowPivotMarker();

	/** Project world position to viewport-local screen coordinates */
	bool ProjectWorldToViewport(const FVector& WorldPosition, FVector2D& OutViewportPosition, FEditorViewportClient* ViewportClient);

	/** Get the current level viewport */
	TSharedPtr<SLevelViewport> GetActiveLevelViewport();

	bool bIsEnabled = false;
	bool bMarkerVisible = false;
	bool bAttachedToViewport = false;
	FDelegateHandle SelectionChangedHandle;

	// Overlay widgets
	TSharedPtr<SCanvas> OverlayCanvas;
	TSharedPtr<SImage> PivotMarkerImage;
	SCanvas::FSlot* PivotMarkerSlot = nullptr;

	// Cached viewport reference
	TWeakPtr<SLevelViewport> CachedLevelViewport;

	// Cached pivot location
	FVector CachedPivotWorldLocation = FVector::ZeroVector;
};
