#pragma once

#include "CoreMinimal.h"
#include "ILevelEditor.h"
#include "Framework/Application/IInputProcessor.h"

class FNavigationController;
class FTransformController;
class FSelectionActionsController;
class UBlenderOrbitInteraction;
class UViewportOrbitInteraction;

/**
 * Input processor for Blender-style controls in Unreal Editor.
 * Acts as a thin dispatcher routing input to specialized controllers.
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

	void ToggleEnabled();
	bool IsEnabled() const { return bIsEnabled; }

private:
	void RegisterInputProcessor();
	void UnregisterInputProcessor();
	void Init(TSharedPtr<ILevelEditor> InLevelEditor);

	bool bIsEnabled = false;
	FVector2D LastMousePosition = FVector2D::ZeroVector;

	// Controllers
	TSharedPtr<FNavigationController> NavigationController;
	TSharedPtr<FTransformController> TransformController;
	TSharedPtr<FSelectionActionsController> SelectionActionsController;
};
