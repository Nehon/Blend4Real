#include "Blend4RealInputProcessor.h"
#include "Blend4RealSettings.h"
#include "Blend4RealUtils.h"
#include "FNavigationController.h"
#include "FTransformController.h"
#include "FSelectionActionsController.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"
#include "EditorModeManager.h"


FBlend4RealInputProcessor::FBlend4RealInputProcessor()
	: bIsEnabled(false)
	  , LastMousePosition(FVector2D::ZeroVector)
{
	// Create controllers
	TransformController = MakeShareable(new FTransformController());
	NavigationController = MakeShareable(new FNavigationController());
	SelectionActionsController = MakeShareable(new FSelectionActionsController(TransformController));

	RegisterInputProcessor();
}

FBlend4RealInputProcessor::~FBlend4RealInputProcessor()
{
	UnregisterInputProcessor();
}

void FBlend4RealInputProcessor::RegisterInputProcessor()
{
	if (bIsEnabled && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().RegisterInputPreProcessor(SharedThis(this));
	}
}

void FBlend4RealInputProcessor::UnregisterInputProcessor()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(SharedThis(this));
	}
}

void FBlend4RealInputProcessor::ToggleEnabled()
{
	bIsEnabled = !bIsEnabled;
	// Toggle transform gizmo visibility (hide when BlenderControls enabled, show when disabled)
	GLevelEditorModeTools().SetShowWidget(!bIsEnabled);

	if (bIsEnabled)
	{
		RegisterInputProcessor();
		UE_LOG(LogTemp, Display, TEXT("Blender Controls: Enabled"));
	}
	else
	{
		UnregisterInputProcessor();
		UE_LOG(LogTemp, Display, TEXT("Blender Controls: Disabled"));
	}
}

void FBlend4RealInputProcessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	// Nothing to do - visualization is updated when transform state changes
}

bool FBlend4RealInputProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	if (!bIsEnabled)
	{
		return false;
	}

	const EModifierKey::Type ModMask = EModifierKey::FromBools(
		InKeyEvent.IsControlDown(),
		InKeyEvent.IsAltDown(),
		InKeyEvent.IsShiftDown(),
		InKeyEvent.IsCommandDown());
	const FKey Key = InKeyEvent.GetKey();

	// Handle transform mode inputs
	if (TransformController->IsTransforming())
	{
		// Axis keys
		ETransformAxis Axis;
		if (Blend4RealUtils::IsAxisKey(InKeyEvent, Axis) && ModMask == 0)
		{
			TransformController->SetAxis(Axis);
			return true;
		}

		// Numeric input
		FString Digit;
		if (Blend4RealUtils::IsNumericKey(InKeyEvent, Digit) && ModMask == 0)
		{
			TransformController->HandleNumericInput(Digit);
			return true;
		}

		// Backspace
		if (Key == EKeys::BackSpace && ModMask == 0)
		{
			TransformController->HandleBackspace();
			return true;
		}

		// Enter/Space applies transform
		if ((Key == EKeys::Enter || Key == EKeys::SpaceBar) && ModMask == 0)
		{
			if (TransformController->IsNumericInputMode())
			{
				TransformController->ApplyNumericTransform();
			}
			TransformController->EndTransform(true);
			return true;
		}

		// Escape cancels transform
		if (Key == EKeys::Escape && ModMask == 0)
		{
			TransformController->EndTransform(false);
			return true;
		}

		return false;
	}

	// Not transforming - check for action keys
	const UBlend4RealSettings* Settings = UBlend4RealSettings::Get();

	// Selection actions
	if (UBlend4RealSettings::MatchesChord(Settings->DuplicateKey, InKeyEvent))
	{
		SelectionActionsController->DuplicateSelectedAndGrab();
		return true;
	}
	if (UBlend4RealSettings::MatchesChord(Settings->DeleteSelectedKey, InKeyEvent))
	{
		SelectionActionsController->DeleteSelected();
		return true;
	}

	// Transform modes
	if (UBlend4RealSettings::MatchesChord(Settings->TranslationKey, InKeyEvent))
	{
		TransformController->BeginTransform(ETransformMode::Translation);
		return true;
	}
	if (UBlend4RealSettings::MatchesChord(Settings->RotationKey, InKeyEvent))
	{
		TransformController->BeginTransform(ETransformMode::Rotation);
		return true;
	}
	if (UBlend4RealSettings::MatchesChord(Settings->ScaleKey, InKeyEvent))
	{
		TransformController->BeginTransform(ETransformMode::Scale);
		return true;
	}

	// Transform reset
	if (UBlend4RealSettings::MatchesChord(Settings->ResetTranslationKey, InKeyEvent))
	{
		TransformController->ResetTransform(ETransformMode::Translation);
		return true;
	}
	if (UBlend4RealSettings::MatchesChord(Settings->ResetRotationKey, InKeyEvent))
	{
		TransformController->ResetTransform(ETransformMode::Rotation);
		return true;
	}
	if (UBlend4RealSettings::MatchesChord(Settings->ResetScaleKey, InKeyEvent))
	{
		TransformController->ResetTransform(ETransformMode::Scale);
		return true;
	}

	return false;
}

bool FBlend4RealInputProcessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	return false;
}

bool FBlend4RealInputProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (!bIsEnabled)
	{
		return false;
	}

	const FVector2D CurrentPosition = MouseEvent.GetScreenSpacePosition();
	const FVector2D Delta = CurrentPosition - LastMousePosition;
	LastMousePosition = CurrentPosition;


	// Handle navigation
	if (NavigationController->IsNavigating())
	{
		if (NavigationController->IsOrbiting())
		{
			NavigationController->UpdateOrbit(Delta);
			return true;
		}
		if (NavigationController->IsPanning())
		{
			NavigationController->UpdatePan(Delta);
			return true;
		}
	}

	// Handle transform
	if (TransformController->IsTransforming() && !TransformController->IsNumericInputMode())
	{
		if (Delta.IsNearlyZero())
		{
			return false;
		}

		const bool bInvertSnap = MouseEvent.IsControlDown() &&
			!MouseEvent.IsAltDown() &&
			!MouseEvent.IsShiftDown() &&
			!MouseEvent.IsCommandDown();

		TransformController->UpdateFromMouseMove(CurrentPosition, bInvertSnap);
		return true;
	}

	return false;
}

bool FBlend4RealInputProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (!bIsEnabled)
	{
		return false;
	}

	const UBlend4RealSettings* Settings = UBlend4RealSettings::Get();

	// Camera navigation (when not transforming)
	if (!TransformController->IsTransforming())
	{
		if (UBlend4RealSettings::MatchesChord(Settings->PanCameraKey, MouseEvent))
		{
			NavigationController->BeginPan();
			return true;
		}
		if (UBlend4RealSettings::MatchesChord(Settings->FocusOnHitKey, MouseEvent))
		{
			return NavigationController->FocusOnMouseHit(FVector2D(MouseEvent.GetScreenSpacePosition()));
		}
		if (UBlend4RealSettings::MatchesChord(Settings->OrbitCameraKey, MouseEvent))
		{
			NavigationController->BeginOrbit(FVector2D(MouseEvent.GetScreenSpacePosition()));
			return true;
		}
	}

	// Transform confirmation
	if (TransformController->IsTransforming())
	{
		if (UBlend4RealSettings::MatchesChord(Settings->ApplyTransformKey, MouseEvent))
		{
			TransformController->EndTransform(true);
			return true;
		}
		if (UBlend4RealSettings::MatchesChord(Settings->CancelTransformKey, MouseEvent))
		{
			TransformController->EndTransform(false);
			return true;
		}
	}

	return false;
}

bool FBlend4RealInputProcessor::HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp,
                                                                  const FPointerEvent& MouseEvent)
{
	if (!bIsEnabled)
	{
		return false;
	}

	// Double-click to focus on hit point
	return NavigationController->FocusOnMouseHit(FVector2D(MouseEvent.GetScreenSpacePosition()));
}

bool FBlend4RealInputProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (!bIsEnabled)
	{
		return false;
	}

	// Middle mouse button ends orbit or pan
	if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		if (NavigationController->IsOrbiting())
		{
			NavigationController->EndOrbit();
			return true;
		}
		if (NavigationController->IsPanning())
		{
			NavigationController->EndPan();
			return true;
		}
	}

	return false;
}
