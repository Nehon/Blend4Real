// Copyright 2025-Present - Nehon (Rémy Bouquet). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ILevelEditor.h"
#include "Framework/Application/IInputProcessor.h"

class FNavigationController;
class FTransformController;
class FSelectionActionsController;
class FPivotVisualizationController;
class UBlenderOrbitInteraction;
class UViewportOrbitInteraction;

/**
 * Input processor for Blender-style controls in Unreal Editor.
 * Acts as a thin dispatcher routing input to specialized controllers.
 *
 * Always registered as a Slate input pre-processor. Supports two activation modes:
 *
 * - Persistent mode: toggled via the toolbar button. Blend4Real stays enabled until
 *   explicitly toggled off. All Blender controls (transforms, navigation, selection) are active.
 *
 * - Transient mode (opt-in via "Instant Blender Controls" setting): pressing a transform key
 *   (G/R/S) while Blend4Real is disabled temporarily activates it for the duration of the
 *   transform. Once confirmed (LMB/Enter/Space) or cancelled (RMB/Escape), Blend4Real
 *   disables itself and returns to standard Unreal controls.
 */
class FBlend4RealInputProcessor : public TSharedFromThis<FBlend4RealInputProcessor>, public IInputProcessor
{
public:
	FBlend4RealInputProcessor();
	virtual ~FBlend4RealInputProcessor() override;

	// IInputProcessor interface
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool
	HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;

	void ToggleEnabled(const bool bInvalidateRender = true);
	bool IsEnabled() const { return bIsEnabled; }

private:
	void RegisterInputProcessor();
	void UnregisterInputProcessor();
	void Init(TSharedPtr<ILevelEditor> InLevelEditor);

	/** End transient mode if active: clears the flag and disables Blend4Real. */
	void EndTransientModeIfActive();

	bool bIsEnabled = false;

	/** When true, Blend4Real was activated by a transform key press and will auto-disable when the transform ends. */
	bool bTransientMode = false;
	bool bCursorHidden = false;
	FVector2D LastMousePosition = FVector2D::ZeroVector;
	FIntPoint PreNavigationCursorPos = FIntPoint::ZeroValue;

	// Controllers
	TSharedPtr<FNavigationController> NavigationController;
	TSharedPtr<FTransformController> TransformController;
	TSharedPtr<FSelectionActionsController> SelectionActionsController;
	TSharedPtr<FPivotVisualizationController> PivotVisualizationController;
};
