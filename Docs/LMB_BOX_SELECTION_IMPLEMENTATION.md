# LMB Box Selection Implementation (Blender-style)

This document describes how to implement Blender-style box selection with LMB drag in the Blend4Real plugin.

## Overview

In Blender, clicking and dragging LMB in empty space creates a selection box. In Unreal, this requires Ctrl+Alt+LMB in perspective viewports.

This implementation adds a new state machine to the input processor that:
1. Detects LMB down on empty space (no actor under cursor)
2. Tracks the drag to draw a selection rectangle
3. On release, selects all actors in the frustum defined by the rectangle

## Changes Required

### File: `Blend4RealInputProcessor.h`

#### 1. Add New State Variables (in the private section, around line 76)

```cpp
	// Box selection state
	bool bIsBoxSelecting = false;
	FVector2D BoxSelectStart = FVector2D::ZeroVector;
	FVector2D BoxSelectEnd = FVector2D::ZeroVector;
```

#### 2. Add New Function Declarations (in the private section)

```cpp
	// Box selection functions
	void BeginBoxSelect(const FVector2D& ScreenPosition);
	void UpdateBoxSelect(const FVector2D& ScreenPosition);
	void EndBoxSelect(bool bAddToSelection);
	void CancelBoxSelect();
	void DrawBoxSelectRect();
	bool IsClickingOnActor() const;
```

---

### File: `Blend4RealInputProcessor.cpp`

#### 1. Add Required Includes

```cpp
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "SceneView.h"
#include "EngineUtils.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Interfaces/TypedElementWorldInterface.h"
#include "LevelEditorSubsystem.h"
#include "ScopedTransaction.h"
```

#### 2. Modify `HandleMouseButtonDownEvent` - Add Box Selection Start

Find the existing function and add box selection handling. Add this code **before** the transform handling section (before `if (!bIsTransforming)`):

```cpp
bool FBlend4RealInputProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (!bIsEnabled)
	{
		return false;
	}

	const EModifierKey::Type ModMask =
		EModifierKey::FromBools(MouseEvent.IsControlDown(),
		                        MouseEvent.IsAltDown(),
		                        MouseEvent.IsShiftDown(),
		                        MouseEvent.IsCommandDown());

	// Left mouse button - check for box selection start
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Don't start box select if we're transforming
		if (!bIsTransforming && !bIsBoxSelecting)
		{
			// Check if we're clicking on empty space (no actor under cursor)
			if (!IsClickingOnActor())
			{
				// Start box selection
				const FVector2D ScreenPos = MouseEvent.GetScreenSpacePosition();
				BeginBoxSelect(ScreenPos);
				return true;
			}
		}

		// If we're box selecting, end it and apply selection
		if (bIsBoxSelecting)
		{
			const bool bAddToSelection = MouseEvent.IsShiftDown();
			EndBoxSelect(bAddToSelection);
			return true;
		}
	}

	// Right mouse button cancels box selection
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && bIsBoxSelecting)
	{
		CancelBoxSelect();
		return true;
	}

	// ... existing orbit/pan and transform handling code continues here ...
```

**Important**: The existing orbit/pan and transform code should come AFTER this new box selection code.

#### 3. Modify `HandleMouseMoveEvent` - Add Box Selection Update

Add this code near the beginning of the function, after the `bIsEnabled` check:

```cpp
	// Box selection drag
	if (bIsBoxSelecting)
	{
		const FVector2D CurrentPosition = MouseEvent.GetScreenSpacePosition();
		UpdateBoxSelect(CurrentPosition);
		return true;
	}
```

#### 4. Modify `HandleMouseButtonUpEvent` - Add Box Selection End

Add handling for LMB release during box selection:

```cpp
bool FBlend4RealInputProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (!bIsEnabled)
	{
		return false;
	}

	// End box selection on LMB release
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsBoxSelecting)
	{
		const bool bAddToSelection = MouseEvent.IsShiftDown();
		EndBoxSelect(bAddToSelection);
		return true;
	}

	// ... existing orbit/pan handling continues ...
```

#### 5. Add `IsClickingOnActor` Function

This function checks if there's an actor under the cursor:

```cpp
bool FBlend4RealInputProcessor::IsClickingOnActor() const
{
	FViewport* Viewport = GEditor->GetActiveViewport();
	if (!Viewport)
	{
		return false;
	}

	FIntPoint MousePos;
	Viewport->GetMousePos(MousePos);

	// Use hit proxy to check if we're clicking on an actor
	HHitProxy* HitProxy = Viewport->GetHitProxy(MousePos.X, MousePos.Y);
	if (HitProxy)
	{
		// Check if the hit proxy is an actor
		if (HitProxy->IsA(HActor::StaticGetType()))
		{
			return true;
		}
		// Also check for component hit proxies
		if (HitProxy->IsA(HComponentVisProxy::StaticGetType()))
		{
			return true;
		}
	}

	return false;
}
```

#### 6. Add `BeginBoxSelect` Function

```cpp
void FBlend4RealInputProcessor::BeginBoxSelect(const FVector2D& ScreenPosition)
{
	bIsBoxSelecting = true;
	BoxSelectStart = ScreenPosition;
	BoxSelectEnd = ScreenPosition;

	// Clear hover effects
	FLevelEditorViewportClient::ClearHoverFromObjects();

	UE_LOG(LogTemp, Display, TEXT("Box Select: Started at (%f, %f)"), ScreenPosition.X, ScreenPosition.Y);
}
```

#### 7. Add `UpdateBoxSelect` Function

```cpp
void FBlend4RealInputProcessor::UpdateBoxSelect(const FVector2D& ScreenPosition)
{
	BoxSelectEnd = ScreenPosition;
	DrawBoxSelectRect();
}
```

#### 8. Add `EndBoxSelect` Function

This is the main selection logic, reusing Unreal's frustum selection system:

```cpp
void FBlend4RealInputProcessor::EndBoxSelect(bool bAddToSelection)
{
	if (!bIsBoxSelecting)
	{
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("Box Select: Ended, AddToSelection=%d"), bAddToSelection);

	FViewport* Viewport = GEditor->GetActiveViewport();
	if (!Viewport)
	{
		CancelBoxSelect();
		return;
	}

	FLevelEditorViewportClient* ViewportClient = static_cast<FLevelEditorViewportClient*>(Viewport->GetClient());
	if (!ViewportClient)
	{
		CancelBoxSelect();
		return;
	}

	// Calculate the selection rect in viewport coordinates
	FVector2D StartLocal = BoxSelectStart;
	FVector2D EndLocal = BoxSelectEnd;

	// Convert screen coordinates to viewport coordinates
	// Get viewport widget position
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<SLevelViewport> ActiveViewport = LevelEditorModule.GetFirstActiveLevelViewport();
	if (ActiveViewport.IsValid())
	{
		FGeometry ViewportGeometry = ActiveViewport->GetTickSpaceGeometry();
		FVector2D ViewportPos = ViewportGeometry.GetAbsolutePosition();
		StartLocal -= ViewportPos;
		EndLocal -= ViewportPos;
	}

	// Normalize the rect (ensure Min < Max)
	FVector2D RectMin(FMath::Min(StartLocal.X, EndLocal.X), FMath::Min(StartLocal.Y, EndLocal.Y));
	FVector2D RectMax(FMath::Max(StartLocal.X, EndLocal.X), FMath::Max(StartLocal.Y, EndLocal.Y));

	// Skip if the selection box is too small (treat as a click)
	if ((RectMax - RectMin).SizeSquared() < 16.0f)
	{
		CancelBoxSelect();
		return;
	}

	// Create scene view for frustum calculation
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport,
		ViewportClient->GetScene(),
		ViewportClient->EngineShowFlags));
	FSceneView* SceneView = ViewportClient->CalcSceneView(&ViewFamily);

	if (!SceneView)
	{
		CancelBoxSelect();
		return;
	}

	// Build frustum from selection rectangle
	FConvexVolume SelectionFrustum;
	{
		// Get the four corners of the selection rect in screen space
		const float MinX = RectMin.X;
		const float MinY = RectMin.Y;
		const float MaxX = RectMax.X;
		const float MaxY = RectMax.Y;

		// Create frustum planes from the rectangle
		FVector FrustumVertices[8];

		// Near plane corners (at depth 0)
		FrustumVertices[0] = SceneView->ScreenToWorld(SceneView->PixelToScreen(MinX, MinY, 0.0f));
		FrustumVertices[1] = SceneView->ScreenToWorld(SceneView->PixelToScreen(MaxX, MinY, 0.0f));
		FrustumVertices[2] = SceneView->ScreenToWorld(SceneView->PixelToScreen(MaxX, MaxY, 0.0f));
		FrustumVertices[3] = SceneView->ScreenToWorld(SceneView->PixelToScreen(MinX, MaxY, 0.0f));

		// Far plane corners (at depth 1)
		FrustumVertices[4] = SceneView->ScreenToWorld(SceneView->PixelToScreen(MinX, MinY, 1.0f));
		FrustumVertices[5] = SceneView->ScreenToWorld(SceneView->PixelToScreen(MaxX, MinY, 1.0f));
		FrustumVertices[6] = SceneView->ScreenToWorld(SceneView->PixelToScreen(MaxX, MaxY, 1.0f));
		FrustumVertices[7] = SceneView->ScreenToWorld(SceneView->PixelToScreen(MinX, MaxY, 1.0f));

		// Build frustum planes (6 planes: near, far, left, right, top, bottom)
		TArray<FPlane> Planes;

		// Left plane (vertices 0, 3, 4, 7)
		Planes.Add(FPlane(FrustumVertices[0], FrustumVertices[4], FrustumVertices[7]));
		// Right plane (vertices 1, 2, 5, 6)
		Planes.Add(FPlane(FrustumVertices[1], FrustumVertices[5], FrustumVertices[6]));
		// Top plane (vertices 0, 1, 4, 5)
		Planes.Add(FPlane(FrustumVertices[0], FrustumVertices[1], FrustumVertices[4]));
		// Bottom plane (vertices 2, 3, 6, 7)
		Planes.Add(FPlane(FrustumVertices[2], FrustumVertices[6], FrustumVertices[3]));
		// Near plane
		Planes.Add(FPlane(FrustumVertices[0], FrustumVertices[1], FrustumVertices[2]));
		// Far plane
		Planes.Add(FPlane(FrustumVertices[4], FrustumVertices[6], FrustumVertices[5]));

		SelectionFrustum.Planes = MoveTemp(Planes);
	}

	// Begin selection transaction
	FScopedTransaction Transaction(NSLOCTEXT("Blend4Real", "BoxSelectTransaction", "Box Select"));

	// Get selection set
	UTypedElementSelectionSet* SelectionSet = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>()->GetSelectionSet();
	SelectionSet->Modify();

	// Clear existing selection if not adding
	if (!bAddToSelection)
	{
		FTypedElementSelectionOptions ClearOptions;
		SelectionSet->ClearSelection(ClearOptions);
	}

	// Find all actors in the frustum
	TArray<FTypedElementHandle> ElementsToSelect;
	UWorld* World = ViewportClient->GetWorld();

	for (FActorIterator It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || !Actor->IsSelectable() || Actor->IsHiddenEd())
		{
			continue;
		}

		// Check if actor bounds intersect with frustum
		FBox ActorBounds = Actor->GetComponentsBoundingBox();
		bool bFullyContained = false;
		if (SelectionFrustum.IntersectBox(ActorBounds.GetCenter(), ActorBounds.GetExtent(), bFullyContained))
		{
			if (FTypedElementHandle ActorHandle = UEngineElementsLibrary::AcquireEditorActorElementHandle(Actor))
			{
				ElementsToSelect.Add(ActorHandle);
			}
		}
	}

	// Apply selection
	if (ElementsToSelect.Num() > 0)
	{
		FTypedElementSelectionOptions SelectOptions;
		SelectionSet->SelectElements(ElementsToSelect, SelectOptions);
		UE_LOG(LogTemp, Display, TEXT("Box Select: Selected %d actors"), ElementsToSelect.Num());
	}

	// Clean up
	FLevelEditorViewportClient::ClearHoverFromObjects();
	bIsBoxSelecting = false;
	GEditor->RedrawLevelEditingViewports();
}
```

#### 9. Add `CancelBoxSelect` Function

```cpp
void FBlend4RealInputProcessor::CancelBoxSelect()
{
	bIsBoxSelecting = false;
	BoxSelectStart = FVector2D::ZeroVector;
	BoxSelectEnd = FVector2D::ZeroVector;
	FLevelEditorViewportClient::ClearHoverFromObjects();
	GEditor->RedrawLevelEditingViewports();
	UE_LOG(LogTemp, Display, TEXT("Box Select: Cancelled"));
}
```

#### 10. Add `DrawBoxSelectRect` Function

This draws the selection rectangle using a floating window (similar to transform info):

```cpp
void FBlend4RealInputProcessor::DrawBoxSelectRect()
{
	// For now, we rely on the viewport redraw to show selection
	// A proper implementation would need viewport overlay drawing
	// which requires hooking into the viewport's render pipeline

	// Trigger viewport redraw
	GEditor->RedrawLevelEditingViewports();
}
```

**Note**: Drawing the selection rectangle properly requires additional work - see "Visual Feedback" section below.

---

### Additional Includes for Build.cs

Make sure the following modules are in the plugin's Build.cs dependencies:

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    // ... existing modules ...
    "TypedElementFramework",
    "TypedElementRuntime",
});
```

---

## Visual Feedback (Selection Rectangle)

The selection rectangle drawing is the trickiest part. There are several approaches:

### Option A: Use Transform Info Window (Simple)

Reuse the existing `TransformInfoWindow` pattern to show selection coordinates. This is functional but not visually ideal.

### Option B: LineBatchComponent (What we use for transforms)

Draw the rectangle using the LineBatcher, but this draws in world space and would need screen-to-world projection for all corners.

### Option C: Viewport Overlay (Proper but Complex)

Hook into the viewport's canvas rendering via `FEditorViewportClient::Draw()`. This requires:
1. Subclassing or extending the viewport client, or
2. Using a custom HUD/canvas widget

### Recommended: Option B with Projection

Since you're already using LineBatcher for transform visualization, extend that to draw the selection box in world space by projecting the screen corners to world space at a fixed depth.

Add to `DrawBoxSelectRect()`:

```cpp
void FBlend4RealInputProcessor::DrawBoxSelectRect()
{
	if (!bIsBoxSelecting)
	{
		return;
	}

	// Get line batcher
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ULineBatchComponent* Batcher = World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent);
	if (!Batcher)
	{
		return;
	}

	// Clear previous box lines
	static constexpr uint32 BOX_SELECT_BATCH_ID = 14521275;  // Different from transform batch
	Batcher->ClearBatch(BOX_SELECT_BATCH_ID);

	// Get viewport and scene view
	FViewport* Viewport = GEditor->GetActiveViewport();
	if (!Viewport)
	{
		return;
	}

	FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(Viewport->GetClient());
	if (!ViewportClient)
	{
		return;
	}

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport, ViewportClient->GetScene(), ViewportClient->EngineShowFlags));
	FSceneView* SceneView = ViewportClient->CalcSceneView(&ViewFamily);
	if (!SceneView)
	{
		return;
	}

	// Convert screen coordinates to viewport-local coordinates
	FVector2D StartLocal = BoxSelectStart;
	FVector2D EndLocal = BoxSelectEnd;

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<SLevelViewport> ActiveViewport = LevelEditorModule.GetFirstActiveLevelViewport();
	if (ActiveViewport.IsValid())
	{
		FGeometry ViewportGeometry = ActiveViewport->GetTickSpaceGeometry();
		FVector2D ViewportPos = ViewportGeometry.GetAbsolutePosition();
		StartLocal -= ViewportPos;
		EndLocal -= ViewportPos;
	}

	// Project corners to world space at a near depth
	const float Depth = 0.01f;  // Very close to camera
	FVector WorldCorners[4];
	WorldCorners[0] = SceneView->ScreenToWorld(SceneView->PixelToScreen(StartLocal.X, StartLocal.Y, Depth));
	WorldCorners[1] = SceneView->ScreenToWorld(SceneView->PixelToScreen(EndLocal.X, StartLocal.Y, Depth));
	WorldCorners[2] = SceneView->ScreenToWorld(SceneView->PixelToScreen(EndLocal.X, EndLocal.Y, Depth));
	WorldCorners[3] = SceneView->ScreenToWorld(SceneView->PixelToScreen(StartLocal.X, EndLocal.Y, Depth));

	// Draw the rectangle
	const FLinearColor BoxColor = FLinearColor::White;
	const float Thickness = 2.0f;

	Batcher->DrawLine(WorldCorners[0], WorldCorners[1], BoxColor, SDPG_Foreground, Thickness, 0.0f, BOX_SELECT_BATCH_ID);
	Batcher->DrawLine(WorldCorners[1], WorldCorners[2], BoxColor, SDPG_Foreground, Thickness, 0.0f, BOX_SELECT_BATCH_ID);
	Batcher->DrawLine(WorldCorners[2], WorldCorners[3], BoxColor, SDPG_Foreground, Thickness, 0.0f, BOX_SELECT_BATCH_ID);
	Batcher->DrawLine(WorldCorners[3], WorldCorners[0], BoxColor, SDPG_Foreground, Thickness, 0.0f, BOX_SELECT_BATCH_ID);

	GEditor->RedrawLevelEditingViewports();
}
```

Add cleanup in `CancelBoxSelect()` and `EndBoxSelect()`:

```cpp
// Clear box selection visualization
UWorld* World = GetWorld();
if (World)
{
    if (ULineBatchComponent* Batcher = World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent))
    {
        static constexpr uint32 BOX_SELECT_BATCH_ID = 14521275;
        Batcher->ClearBatch(BOX_SELECT_BATCH_ID);
    }
}
```

---

## Behavior Summary

| Action | Result |
|--------|--------|
| LMB click on actor | Normal Unreal selection (passed through) |
| LMB click on empty space | Start box selection |
| LMB drag | Update selection box |
| LMB release | Apply selection to actors in box |
| Shift + LMB release | Add to existing selection |
| RMB during box select | Cancel box selection |
| ESC during box select | Cancel box selection |

---

## Notes

1. **Hit Proxy Check**: The `IsClickingOnActor()` function uses hit proxies which require the viewport to have been rendered. If there are issues, consider using a raycast instead.

2. **Coordinate Systems**: Screen coordinates from Slate are in absolute screen space. They need to be converted to viewport-local coordinates for proper frustum calculation.

3. **Performance**: The actor iteration in `EndBoxSelect` could be optimized using spatial queries if needed for large scenes.

4. **Selection System**: This implementation uses Unreal's TypedElementSelectionSet for proper integration with the editor's selection system and undo/redo.
