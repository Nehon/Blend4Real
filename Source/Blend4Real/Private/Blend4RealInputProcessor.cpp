#include "Blend4RealInputProcessor.h"
#include "Blend4RealSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "Engine/Selection.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "DrawDebugHelpers.h"
#include "Math/UnrealMathUtility.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "Components/LineBatchComponent.h"
#include "Slate/SceneViewport.h"


#if ORBIT_IMPLEM_ITF
#include "BlenderOrbitInteraction.h"
#include "Editor/Experimental/EditorInteractiveToolsFramework/Public/ViewportInteractions/ViewportInteractionsBehaviorSource.h"
#include "Editor/Experimental/EditorInteractiveToolsFramework/Public/ViewportInteractions/ViewportOrbitInteraction.h"
#include "ViewportInteractions/ViewportInteractionsBehaviorSource.h"
#include "ViewportInteractions/ViewportInteractionsUtilities.h"
#include "HAL/IConsoleManager.h"
#endif

FBlend4RealInputProcessor::FBlend4RealInputProcessor()
	: bIsEnabled(false)
	  , bIsTransforming(false)
	  , bIsNumericInput(false)
	  , CurrentMode(ETransformMode::None)
	  , CurrentAxis(None)
	  , NumericBuffer(TEXT(""))
	  , LastMousePosition(FVector2D::ZeroVector)
{
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
#if ORBIT_IMPLEM_ITF
		EnableBlenderOrbit();
#endif
		RegisterInputProcessor();
		UE_LOG(LogTemp, Display, TEXT("Blender Controls: Enabled"));
	}
	else
	{
#if ORBIT_IMPLEM_ITF
		DisableBlenderOrbit();
#endif
		UnregisterInputProcessor();
		UE_LOG(LogTemp, Display, TEXT("Blender Controls: Disabled"));
	}
}

#if ORBIT_IMPLEM_ITF
void FBlend4RealInputProcessor::EnableBlenderOrbit()
{
	// Enable the experimental ITF viewport interactions system
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(
		TEXT("ViewportInteractions.EnableITFInteractions")))
	{
		CVar->Set(1);
	}

	UModeManagerInteractiveToolsContext* ToolsContext = GLevelEditorModeTools().GetInteractiveToolsContext();
	if (!ToolsContext)
	{
		UE_LOG(LogTemp, Display, TEXT("No tools Context"));
		return;
	}

	UViewportInteractionsBehaviorSource* BehaviorSource = ToolsContext->GetViewportInteractionsBehaviorSource();
	if (!BehaviorSource)
	{
		UE_LOG(LogTemp, Display, TEXT("No tools Behavior source"));
		return;
	}

	// Disable the original orbit interaction
	if (UViewportOrbitInteraction* OriginalOrbit = BehaviorSource->GetTypedViewportInteraction<
		UViewportOrbitInteraction>())
	{
		// Only store if it's not our custom one
		if (!OriginalOrbit->IsA<UBlenderOrbitInteraction>())
		{
			OriginalOrbitInteraction = OriginalOrbit;
			OriginalOrbit->SetEnabled(false);
		}
	}

	// Add our Blender-style orbit interaction
	if (!BlenderOrbitInteraction.IsValid())
	{
		UBlenderOrbitInteraction* NewOrbit = BehaviorSource->AddInteraction<UBlenderOrbitInteraction>(true);
		if (NewOrbit)
		{
			BlenderOrbitInteraction = NewOrbit;
			NewOrbit->SetEnabled(true);
		}
	}
}

void FBlend4RealInputProcessor::DisableBlenderOrbit()
{
	// Disable our custom orbit
	if (BlenderOrbitInteraction.IsValid())
	{
		BlenderOrbitInteraction->SetEnabled(false);
	}

	// Re-enable original orbit
	if (OriginalOrbitInteraction.IsValid())
	{
		OriginalOrbitInteraction->SetEnabled(true);
	}

	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(
		TEXT("ViewportInteractions.EnableITFInteractions")))
	{
		CVar->Set(0);
	}
}
#endif


FTransform ComputeTransformPivot()
{
	// TODO allow for individual origins / 3D cursor
	//  For now only median point

	if (!GEditor)
	{
		return FTransform();
	}

	USelection* SelectedActors = GEditor->GetSelectedActors();
	FVector Center(0.0);
	int Count = 0;
	FTransform Transform = FTransform();


	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		if (const AActor* Actor = Cast<AActor>(*It))
		{
			// Get the current transform
			FTransform ActorTransform = Actor->GetActorTransform();
			Center += ActorTransform.GetLocation();
			Count++;
		}
	}
	Center /= Count;
	Transform.SetLocation(Center);
	return Transform;
}

#if ORBIT_PAN_IMPLEM_INLINE
void FBlend4RealInputProcessor::BeginOrbit(const FVector2D MousePosition)
{
	FViewport* Viewport = GEditor->GetActiveViewport();
	if (!Viewport)
	{
		return;
	}

	FLevelEditorViewportClient* ViewportClient = static_cast<FLevelEditorViewportClient*>(Viewport->GetClient());
	if (!ViewportClient || !ViewportClient->IsPerspective())
	{
		return;
	}

	bIsOrbiting = true;
	LastMousePosition = FSlateApplication::Get().GetCursorPos();


	// Store initial camera state
	OrbitPivot = ViewportClient->GetLookAtLocation();

	// Get orbit mode from settings
	const UBlend4RealSettings* Settings = UBlend4RealSettings::Get();
	const USelection* SelectedActors = GEditor->GetSelectedActors();

	if (Settings->ShouldOrbitAroundMouseHit())
	{
		const FHitResult Result = GetScenePick(MousePosition);
		if (Result.IsValidBlockingHit())
		{
			OrbitPivot = Result.Location;
		}
	}
	else if (Settings->ShouldOrbitAroundSelection() && SelectedActors && SelectedActors->Num() > 0)
	{
		// Override pivot with selection center if something is selected
		OrbitPivot = ComputeTransformPivot().GetLocation();
	}
}

void FBlend4RealInputProcessor::EndOrbit()
{
	bIsOrbiting = false;
}

void FBlend4RealInputProcessor::BeginPan()
{
	FViewport* Viewport = GEditor->GetActiveViewport();
	if (!Viewport)
	{
		return;
	}

	FLevelEditorViewportClient* ViewportClient = static_cast<FLevelEditorViewportClient*>(Viewport->GetClient());
	if (!ViewportClient)
	{
		return;
	}
	bIsPanning = true;
	LastMousePosition = FSlateApplication::Get().GetCursorPos();
}

void FBlend4RealInputProcessor::EndPan()
{
	bIsPanning = false;
}

void FBlend4RealInputProcessor::OrbitCamera(const FVector2D Delta,
                                            FLevelEditorViewportClient* ViewportClient) const
{
	if (!ViewportClient)
	{
		return;
	}
	constexpr float RotationSpeed = 0.25f;

	// Calculate rotation delta
	const float DeltaYaw = Delta.X * RotationSpeed;
	const float DeltaPitch = Delta.Y * RotationSpeed;

	// Get current camera state
	const FVector CameraLocation = ViewportClient->GetViewLocation();
	const FRotator CameraRotation = ViewportClient->GetViewRotation();

	// Calculate offset from pivot to camera
	FVector Offset = CameraLocation - OrbitPivot;

	// Create rotation for yaw (around world Z) and pitch (around camera right)
	const FRotator YawRotation(0, DeltaYaw, 0);
	const FVector RightVector = FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Y);
	const float NewPitch = FMath::Clamp(CameraRotation.Pitch - DeltaPitch, -89.0f, 89.0f);
	const float ClampedPitch = CameraRotation.Pitch - NewPitch;
	FQuat PitchQuat = FQuat(RightVector, FMath::DegreesToRadians(ClampedPitch));

	// Rotate the offset
	Offset = YawRotation.RotateVector(Offset);
	Offset = PitchQuat.RotateVector(Offset);

	// Calculate new camera position
	const FVector NewLocation = OrbitPivot + Offset;

	// Rotate the camera view direction by the same amount
	FRotator NewRotation = CameraRotation;
	NewRotation.Yaw += DeltaYaw;
	NewRotation.Pitch = NewPitch;

	ViewportClient->SetViewLocation(NewLocation);
	ViewportClient->SetViewRotation(NewRotation);

	GEditor->RedrawLevelEditingViewports();
}

void FBlend4RealInputProcessor::PanCamera(const FVector2D Delta,
                                          FLevelEditorViewportClient* ViewportClient) const
{
	if (!ViewportClient)
	{
		return;
	}
	constexpr float PanSpeed = 1.0f;

	const FRotator CameraRotation = ViewportClient->GetViewRotation();
	const FVector CameraLocation = ViewportClient->GetViewLocation();

	// Get camera right and up vectors
	const FVector RightVector = FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Y);
	const FVector UpVector = FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Z);

	// Pan the camera (move opposite to mouse direction for natural feel)
	const FVector PanDelta = (-RightVector * Delta.X + UpVector * Delta.Y) * PanSpeed;

	ViewportClient->SetViewLocation(CameraLocation + PanDelta);

	GEditor->RedrawLevelEditingViewports();
}


#endif


UWorld* GetWorld()
{
	return GEditor->GetActiveViewport()->GetClient()->GetWorld();
}

static const FColor AxisColor[] = {
	FColor::Black, FColor::Red, FColor::Green, FColor::Blue, FColor::Red, FColor::Green, FColor::Blue
};

void FBlend4RealInputProcessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	// Tick doesn't need to do line drawing - lines are updated in UpdateTransformVisualization()
	// which is called when transform state actually changes
}


FSceneView* GetScene()
{
	const auto* Viewport = GEditor->GetActiveViewport();
	FEditorViewportClient* EClient = static_cast<FEditorViewportClient*>(Viewport->GetClient());
	FSceneViewFamily ViewFamily = FSceneViewFamily::ConstructionValues(Viewport, EClient->GetScene(),
	                                                                   EClient->EngineShowFlags);
	return EClient->CalcSceneView(&ViewFamily);
}

FPlane FBlend4RealInputProcessor::ComputePlane(const FVector& InitialPos)
{
	TransformViewDir = GetScene()->GetViewDirection().GetSafeNormal();
	const FVector Axis = GetAxisVector(CurrentAxis);
	const float DotVal = abs(FVector::DotProduct(TransformViewDir, Axis));
	if (CurrentMode == ETransformMode::Translation && CurrentAxis != None && DotVal > 0.3 && DotVal <= 0.96)
	{
		if (CurrentAxis == WorldZ)
		{
			TransformViewDir.Z = 0.0;
			TransformViewDir = TransformViewDir.GetSafeNormal();
			const FPlane ZeroPlane(TransformViewDir, 0.0);
			const float Dist = FMath::RayPlaneIntersectionParam(InitialPos, TransformViewDir, ZeroPlane);
			const FVector Normal = -TransformViewDir;
			return FPlane(Normal, Dist);
		}
		const float Dist = TransformPivot.GetLocation().Z;
		const FVector Normal(0.0, 0.0, 1.0);
		return FPlane(Normal, Dist);
	}
	const FPlane ZeroPlane(TransformViewDir, 0.0);
	const float Dist = FMath::RayPlaneIntersectionParam(InitialPos, TransformViewDir, ZeroPlane);
	const FVector Normal = -TransformViewDir;
	return FPlane(Normal, Dist);
}

void FBlend4RealInputProcessor::DuplicateSelectedAndGrab()
{
	if (!GEditor || !GUnrealEd)
	{
		return;
	}

	const USelection* SelectedActors = GEditor->GetSelectedActors();
	if (!SelectedActors || SelectedActors->Num() == 0)
	{
		// No actors to duplicate
		return;
	}

	// Get the current world
	const UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return;
	}

	// Use Unreal's built-in duplication which:
	// 1. Duplicates selected actors
	// 2. Automatically selects the new duplicates
	// 3. Handles undo/redo properly
	// bOffsetLocations = false so duplicates are created at the same position
	// (we'll move them with the grab mode)
	GUnrealEd->edactDuplicateSelected(World->GetCurrentLevel(), false);

	// Now the new actors are selected, enter translation mode immediately
	BeginTransform(ETransformMode::Translation);
}

void FBlend4RealInputProcessor::DeleteSelected()
{
	if (!GEditor || !GUnrealEd)
	{
		return;
	}

	const USelection* SelectedActors = GEditor->GetSelectedActors();

	if (!SelectedActors || SelectedActors->Num() == 0)
	{
		// No actors to duplicate
		return;
	}

	// Get the current world
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return;
	}
	GEditor->BeginTransaction(TEXT(""), FText::FromString("Remove Selected Actors"), nullptr);
	SetSelectionAsModified();
	// Use Unreal's built-in deletion 
	GUnrealEd->edactDeleteSelected(World);
	GEditor->EndTransaction();
}

bool FBlend4RealInputProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	if (!bIsEnabled)
	{
		return false;
	}

	// construct a Modifier bit mask to easily check strict modifier down (like ONLY shift or Only Shift and Ctrl)
	const EModifierKey::Type ModMask = // 
		EModifierKey::FromBools(InKeyEvent.IsControlDown(),
		                        InKeyEvent.IsAltDown(),
		                        InKeyEvent.IsShiftDown(),
		                        InKeyEvent.IsCommandDown());
	const FKey Key = InKeyEvent.GetKey();

	// Check if we're currently in transform mode
	if (bIsTransforming)
	{
		// Check for axis keys
		ETransformAxis Axis;
		if (IsAxisKey(InKeyEvent, Axis) && ModMask == 0)
		{
			SetTransformAxis(Axis);
			const FPlane HitPlane = ComputePlane(TransformPivot.GetLocation());
			DragInitialProjectedPosition = GetPlaneHit(HitPlane.GetNormal(), HitPlane.W);
			TransformSelectedActors(FVector(0.0), 0, false);
			return true;
		}

		// Check for numeric input
		FString Digit;
		if (IsNumericKey(InKeyEvent, Digit) && ModMask == 0)
		{
			if (CurrentAxis == None)
			{
				// force numerical transform on X if no axis has been defined
				CurrentAxis = WorldX;
			}
			bIsNumericInput = true;
			NumericBuffer.Append(Digit);
			ApplyNumericTransform();
			UE_LOG(LogTemp, Display, TEXT("Numeric Input: %s"), *NumericBuffer);
			return true;
		}
		if (Key == EKeys::BackSpace && ModMask == 0)
		{
			if (!NumericBuffer.IsEmpty())
			{
				// if we are typing numeric input backspace removes a character
				NumericBuffer.RemoveAt(NumericBuffer.Len() - 1);
				ApplyNumericTransform();
				UE_LOG(LogTemp, Display, TEXT("Numeric Input: %s"), *NumericBuffer);
			}
			// Still eat the input else it can delete the actor 
			return true;
		}

		// Enter key applies numeric transform
		if ((Key == EKeys::Enter || Key == EKeys::SpaceBar) && ModMask == 0)
		{
			if (bIsNumericInput && !NumericBuffer.IsEmpty())
			{
				ApplyNumericTransform();
			}
			EndTransform(true);
			return true;
		}

		// Escape key cancels transform
		if (Key == EKeys::Escape && ModMask == 0)
		{
			EndTransform(false);
			return true;
		}

		return false;
	}


	// Get settings for configurable keybindings
	const UBlend4RealSettings* Settings = UBlend4RealSettings::Get();

	// Object actions - use configurable keys
	if (UBlend4RealSettings::MatchesChord(Settings->DuplicateKey, InKeyEvent))
	{
		DuplicateSelectedAndGrab();
		return true;
	}
	if (UBlend4RealSettings::MatchesChord(Settings->DeleteSelectedKey, InKeyEvent))
	{
		DeleteSelected();
		return true;
	}

	// Transform modes - use configurable keys
	if (UBlend4RealSettings::MatchesChord(Settings->TranslationKey, InKeyEvent))
	{
		BeginTransform(ETransformMode::Translation);
		return true;
	}
	if (UBlend4RealSettings::MatchesChord(Settings->RotationKey, InKeyEvent))
	{
		BeginTransform(ETransformMode::Rotation);
		return true;
	}
	if (UBlend4RealSettings::MatchesChord(Settings->ScaleKey, InKeyEvent))
	{
		BeginTransform(ETransformMode::Scale);
		return true;
	}

	// Transform reset - use configurable keys
	if (UBlend4RealSettings::MatchesChord(Settings->ResetTranslationKey, InKeyEvent))
	{
		ResetSelectedActorsTransform(ETransformMode::Translation);
		return true;
	}
	if (UBlend4RealSettings::MatchesChord(Settings->ResetRotationKey, InKeyEvent))
	{
		ResetSelectedActorsTransform(ETransformMode::Rotation);
		return true;
	}
	if (UBlend4RealSettings::MatchesChord(Settings->ResetScaleKey, InKeyEvent))
	{
		ResetSelectedActorsTransform(ETransformMode::Scale);
		return true;
	}

	return false;
}

bool FBlend4RealInputProcessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	// We don't need to handle key up events in most cases
	return false;
}


FVector FBlend4RealInputProcessor::GetPlaneHit(const FVector& Normal, const float Distance)
{
	auto* Viewport = GEditor->GetActiveViewport();
	FIntPoint MousePos;
	Viewport->GetMousePos(MousePos);
	const FSceneView* Scene = GetScene();
	Scene->DeprojectFVector2D(MousePos, RayOrigin, RayDirection);
	// DrawDebugPoint(GEditor->GetActiveViewport()->GetClient()->GetWorld(), RayOrigin, 10.0f, FColor::Red, false,
	//                5.0);
	const FPlane Plane(Normal.X, Normal.Y, Normal.Z, Distance);
	const FVector Hit = FMath::RayPlaneIntersection(RayOrigin, RayDirection, Plane);
	return Hit;
}

FHitResult FBlend4RealInputProcessor::ProjectToSurface(const FVector& Start, const FVector& Direction,
                                                       const FCollisionQueryParams& Params) const
{
	FHitResult HitResult;
	const FVector End = Start + Direction * 100000.f;

	if (GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, Params))
	{
		//DrawDebugPoint(GetWorld(), HitResult.Location, 4, FColor::Green, false, 5, 1);
		return HitResult;
	}
	return FHitResult();
}

bool FBlend4RealInputProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (!bIsEnabled)
	{
		return false;
	}

	// Calculate mouse delta
	const FVector2D CurrentPosition = MouseEvent.GetScreenSpacePosition();
	const FVector2D Delta = CurrentPosition - LastMousePosition;
	LastMousePosition = CurrentPosition;

#if ORBIT_PAN_IMPLEM_INLINE

	if (bIsOrbiting || bIsPanning)
	{
		// Camera movement : Orbit + Pan
		const FViewport* Viewport = GEditor->GetActiveViewport();
		if (Viewport)
		{
			FLevelEditorViewportClient* ViewportClient = static_cast<FLevelEditorViewportClient*>(Viewport->
				GetClient());
			if (bIsOrbiting)
			{
				// Handle orbit with middle mouse button
				OrbitCamera(Delta, ViewportClient);
				return true;
			}

			if (bIsPanning)
			{
				// Handle pan with shift + middle mouse button
				PanCamera(Delta, ViewportClient);
				return true;
			}
		}
	}

#endif

	// Transform code
	if (!bIsTransforming || bIsNumericInput)
	{
		return false;
	}

	// Handle transformation
	const FPlane HitPlane = ComputePlane(TransformPivot.GetLocation());
	HitLocation = GetPlaneHit(HitPlane.GetNormal(), HitPlane.W);

	// DrawDebugSolidPlane(GEditor->GetActiveViewport()->GetClient()->GetWorld(), HitPlane,
	//                     DragInitialActorTransform.GetLocation(), 1000.0, FColor(200, 200, 0, 80), false, 3.0);
	// DrawDebugPoint(GEditor->GetActiveViewport()->GetClient()->GetWorld(), HitLocation, 20.0f, FColor::Red, false,
	//                50.0);

	if (Delta.IsNearlyZero())
	{
		return false;
	}

	const FVector AxisVector = GetAxisVector(CurrentAxis);
	auto Mods = EModifierKey::FromBools(MouseEvent.IsControlDown(), MouseEvent.IsAltDown(), MouseEvent.IsShiftDown(),
	                                    MouseEvent.IsCommandDown());
	const bool InvertSnapState = Mods == EModifierKey::Control; // Only Control

	if (CurrentMode == ETransformMode::Rotation)
	{
		const FVector Dir = (TransformPivot.GetLocation() - HitLocation).GetSafeNormal();
		const FVector OriginalDir = (TransformPivot.GetLocation() - DragInitialProjectedPosition).
			GetSafeNormal();

		const float X = FVector::DotProduct(Dir, OriginalDir);
		const FVector Normal = TransformViewDir * (signbit(TransformViewDir.Dot(AxisVector)) == 1 ? -1.0 : 1.0);
		const FVector Tangent = FVector::CrossProduct(OriginalDir, Normal);
		const float Y = FVector::DotProduct(Dir, Tangent);
		const float Angle = FMath::RadiansToDegrees(atan2f(Y, X));
		ApplyTransform(AxisVector, Angle, InvertSnapState);
		UpdateTransformVisualization();
		return true;
	}

	if (CurrentMode == ETransformMode::Scale)
	{
		const ETransformAxis Axis = static_cast<ETransformAxis>(CurrentAxis >= LocalX ? CurrentAxis - 3 : CurrentAxis);

		const float NewDistance = (TransformPivot.GetLocation() - HitLocation).Length();
		if (InitialScaleDistance < 0.001)
		{
			return true;
		}
		const float Scale = NewDistance / InitialScaleDistance;
		ApplyTransform(GetAxisVector(Axis), Scale, InvertSnapState);
		UpdateTransformVisualization();
		return true;
	}


	// Translation
	if (CurrentAxis == None)
	{
		const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();
		// Only project if enabled and transforming 1 Actor only
		const bool Project = ViewportSettings->SnapToSurface.bEnabled && ActorsTransformMap.Array().Num() == 1;

		if (Project)
		{
			const FHitResult Result = ProjectToSurface(RayOrigin, RayDirection, IgnoreSelectionQueryParams);
			if (Result.IsValidBlockingHit())
			{
				const bool AlignToNormal = ViewportSettings->SnapToSurface.bSnapRotation;
				const float Offset = ViewportSettings->SnapToSurface.SnapOffsetExtent;
				FVector Location = Result.Location + Result.Normal * Offset;
				const bool IsSnapTrEnabled = InvertSnapState
					                             ? !ViewportSettings->GridEnabled
					                             : ViewportSettings->GridEnabled;
				if (IsSnapTrEnabled)
				{
					FVector SnappedLocation = Location - TransformPivot.GetLocation();
					const float GridSize = GEditor->GetGridSize();

					SnappedLocation = FVector(ceilf(SnappedLocation.X / GridSize) * GridSize,
					                          ceilf(SnappedLocation.Y / GridSize) * GridSize,
					                          ceilf(SnappedLocation.Z / GridSize) * GridSize);
					Location = SnappedLocation + TransformPivot.GetLocation();
					// keep original rotation when surface snapping
					SetDirectTransformToSelectedActor(&Location);
				}
				else if (AlignToNormal)

				{
					const FRotator SurfaceRotation = Result.Normal.Rotation();
					SetDirectTransformToSelectedActor(&Location, &SurfaceRotation);
				}
				else
				{
					SetDirectTransformToSelectedActor(&Location);
				}
			}
		}
		else
		{
			// no specific axis: moving on the camera aligned plane on the selection
			FVector Dir = (DragInitialProjectedPosition - HitLocation);
			const float TransformValue = Dir.Length();
			Dir /= TransformValue; // Normalization
			ApplyTransform(Dir, -TransformValue, InvertSnapState);
		}
	}
	else
	{
		// Specific axis selected
		const auto* Scene = GetScene();
		const FVector ViewDir = Scene->GetViewDirection().GetSafeNormal();
		// the axis is almost aligned to the camera, projecting HitLocation on it won't work
		// we fall back to the up vector of the camera
		const bool IsAlignedWithCamera = abs(FVector::DotProduct(ViewDir, AxisVector)) > 0.96;
		const FVector Axis = IsAlignedWithCamera ? Scene->GetViewUp() : AxisVector;
		// Compute value and apply transforms
		const float TransformValue = Axis.Dot(DragInitialProjectedPosition - HitLocation);
		ApplyTransform(AxisVector, -TransformValue, InvertSnapState);
	}

	UpdateTransformVisualization();
	return true;
}

FHitResult FBlend4RealInputProcessor::GetScenePick(const FVector2D MouseEventPosition)
{
	// Check if mouse is actually over the viewport widget
	// This may look convoluted but this is actually the most reliable way I could find.
	// The ActiveLevelViewport has a IsHovered method, but it's unreliable as it seems to be based on enter / leave events 
	// instead of bounds checks
	// When you left-click drag (UE move around navigation) the cursor is hidden and, depending on the movement, may get out of the viewport.
	// When releasing the button the cursor seems to be software relocated at the center of the screen, and the enter event is not triggerred, 
	// resulting in the hover state not being set.
	// From that point on you need to exit and re-enter the viewport with the mouse cursor for it to be marked as hovered.
	// After that, getting the actual size and position of a Slate widget is so convoluted that it result in the following code.
	// Important : Note that the passed MouseEventPosition is the one from the mouse event, because it's actually the absolute 
	// mouse position (coordinates in the entire editor window).
	// you may note that we have a Viewport->GetMousePos bellow, but this one is once again a strange method that returns 
	// the last absolute position of the mouse recorded on the viewport.
	// So when the mouse is outside it returns the last position recorded when the mouse was over the viewport -_-
	// So once again, unreliable for bounds check, However it automatically converts the coordinates in local viewport space,
	// which is what we need for ray picking 
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	const TSharedPtr<SLevelViewport> LevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
	if (!LevelViewport.IsValid())
	{
		return FHitResult();
	}
	// get the widget cached geometry, hopefully not likely to change much on a frame basis.
	auto Geometry = LevelViewport->GetCachedGeometry();
	auto LocalSize = Geometry.GetLocalSize();
	auto Min = Geometry.LocalToAbsolute(FVector2D(0, 0)); //Top Left
	auto Max = Geometry.LocalToAbsolute(LocalSize); // Bottom Right

	if (MouseEventPosition.X < Min.X || MouseEventPosition.X > Max.X || MouseEventPosition.Y < Min.Y ||
		MouseEventPosition.Y > Max.Y)
	{
		// mouse is outside the viewport bounds (for sure this time)
		return FHitResult();
	}
	///
	FIntPoint MousePos;
	auto* Viewport = GEditor->GetActiveViewport();
	Viewport->GetMousePos(MousePos);
	FEditorViewportClient* EClient = static_cast<FEditorViewportClient*>(Viewport->GetClient());
	FSceneViewFamily ViewFamily = FSceneViewFamily::ConstructionValues(Viewport, EClient->GetScene(),
	                                                                   EClient->EngineShowFlags);
	// TODO do we really need to invert DPI scale?
	MousePos /= EClient->GetDPIScale();

	const FSceneView* Scene = EClient->CalcSceneView(&ViewFamily);

	Scene->DeprojectFVector2D(MousePos, RayOrigin, RayDirection);
	FCollisionQueryParams Params;
	Params.bTraceComplex = true;
	return ProjectToSurface(RayOrigin, RayDirection, Params);
}

bool FBlend4RealInputProcessor::FocusOnMouseHit(const FVector2D MousePosition)
{
	const FHitResult Result = GetScenePick(MousePosition);

	if (!Result.IsValidBlockingHit())
	{
		return false;
	}
	float BoxSize = 500.f;
	if (Result.Distance < 1800.0)
	{
		BoxSize = 250;
	}
	if (Result.Distance < 1000.0)
	{
		BoxSize = 100;
	}
	FBox Bounds;
	const FVector Extent = FVector(BoxSize);
	Bounds.Min = Result.Location - Extent;
	Bounds.Max = Result.Location + Extent;

	FEditorViewportClient* EClient = static_cast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
	EClient->FocusViewportOnBox(Bounds);
	return true;
}

bool FBlend4RealInputProcessor::HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp,
                                                                  const FPointerEvent& MouseEvent)
{
	if (!bIsEnabled)
	{
		return false;
	}

	// Sketchfab like focus on hit when double click
	return FocusOnMouseHit(FVector2D(MouseEvent.GetScreenSpacePosition()));
}

bool FBlend4RealInputProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (!bIsEnabled)
	{
		return false;
	}

	// Get settings for configurable keybindings
	const UBlend4RealSettings* Settings = UBlend4RealSettings::Get();

#if ORBIT_PAN_IMPLEM_INLINE

	// Camera navigation (when not transforming) - use configurable keys
	if (!bIsTransforming)
	{
		if (UBlend4RealSettings::MatchesChord(Settings->PanCameraKey, MouseEvent))
		{
			BeginPan();
			return true;
		}
		if (UBlend4RealSettings::MatchesChord(Settings->FocusOnHitKey, MouseEvent))
		{
			return FocusOnMouseHit(FVector2D(MouseEvent.GetScreenSpacePosition()));
		}
		if (UBlend4RealSettings::MatchesChord(Settings->OrbitCameraKey, MouseEvent))
		{
			BeginOrbit(FVector2D(MouseEvent.GetScreenSpacePosition()));
			return true;
		}
	}

#endif

	if (!bIsTransforming)
	{
		return false;
	}

	// Transform confirmation - use configurable keys
	if (UBlend4RealSettings::MatchesChord(Settings->ApplyTransformKey, MouseEvent))
	{
		EndTransform(true);
		return true;
	}
	if (UBlend4RealSettings::MatchesChord(Settings->CancelTransformKey, MouseEvent))
	{
		EndTransform(false);
		return true;
	}

	return false;
}

bool FBlend4RealInputProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (!bIsEnabled)
	{
		return false;
	}
#if ORBIT_PAN_IMPLEM_INLINE
	// Middle mouse button ends orbit or pan
	if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		if (bIsOrbiting)
		{
			EndOrbit();
			return true;
		}
		if (bIsPanning)
		{
			EndPan();
			return true;
		}
	}
#endif
	return false;
}

void FBlend4RealInputProcessor::BeginTransform(const ETransformMode Mode)
{
	if (!GEditor || bIsTransforming)
	{
		return;
	}

	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (SelectedActors->Num() == 0)
	{
		//No actor to transform
		return;
	}

	// Save and change selection outline color
	if (GEngine)
	{
		OriginalSelectionColor = GEngine->GetSelectionOutlineColor();
		GEngine->SetSelectionOutlineColor(FLinearColor(1.0f, 1.0f, 1.0f)); // White  during transform
	}
	bIsTransforming = true;
	CurrentMode = Mode;
	CurrentAxis = None; // Default to no axis
	bIsNumericInput = false;
	NumericBuffer.Empty();

	// Capture current mouse position for relative movement
	if (FSlateApplication::IsInitialized())
	{
		LastMousePosition = FSlateApplication::Get().GetCursorPos();
	}

	// Log the mode
	FString ModeText;
	switch (Mode)
	{
	case ETransformMode::Translation:
		{
			ModeText = TEXT("Move Actors");
			TransformPivot = ComputeTransformPivot();
			const FPlane HitPlane = ComputePlane(TransformPivot.GetLocation());
			DragInitialProjectedPosition = GetPlaneHit(HitPlane.GetNormal(), HitPlane.W);
		}
		break;
	case ETransformMode::Rotation:
		{
			ModeText = TEXT("Rotate Actors");
			TransformPivot = ComputeTransformPivot();
			const FPlane HitPlane = ComputePlane(TransformPivot.GetLocation());
			DragInitialProjectedPosition = GetPlaneHit(HitPlane.GetNormal(), HitPlane.W);
			HitLocation = DragInitialProjectedPosition;
		}
		break;
	case ETransformMode::Scale:
		{
			ModeText = TEXT("Scale Actors");
			TransformPivot = ComputeTransformPivot();
			const FPlane HitPlane = ComputePlane(TransformPivot.GetLocation());
			DragInitialProjectedPosition = GetPlaneHit(HitPlane.GetNormal(), HitPlane.W);
			InitialScaleDistance = (DragInitialProjectedPosition - TransformPivot.GetLocation()).Length();
			HitLocation = DragInitialProjectedPosition;
		}
		break;
	default:
		ModeText = TEXT("Unknown");
		break;
	}

	UE_LOG(LogTemp, Display, TEXT("Begin Transform: %s"), *ModeText);
	// Mak all selected actors as modified
	TransactionIndex = GEditor->BeginTransaction(TEXT(""), FText::FromString(ModeText), nullptr);
	ActorsTransformMap.Empty();
	IgnoreSelectionQueryParams.bTraceComplex = true;
	IgnoreSelectionQueryParams.ClearIgnoredSourceObjects();
	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			Actor->Modify();
			ActorsTransformMap.Add(Actor->GetUniqueID(), Actor->GetActorTransform());
			IgnoreSelectionQueryParams.AddIgnoredSourceObject(Actor);
		}
	}
}

void FBlend4RealInputProcessor::EndTransform(bool bApply)
{
	if (!bIsTransforming)
	{
		return;
	}
	HideTransformInfo();
	// Restore original selection outline color
	if (GEngine)
	{
		GEngine->SetSelectionOutlineColor(OriginalSelectionColor);
	}

	if (!bApply)
	{
		// Restore original transforms
		ApplyTransform(FVector(0.0, 0.0, 0.0), 0);
		GEditor->CancelTransaction(TransactionIndex);
		TransactionIndex = -1;
		UE_LOG(LogTemp, Display, TEXT("Transform Cancelled"));
	}
	else
	{
		GEditor->EndTransaction();
		TransactionIndex = -1;
		UE_LOG(LogTemp, Display, TEXT("Transform Applied"));
	}
	FScopedTransaction Transaction(FText::FromString(TEXT("Move Actors")));

	ActorsTransformMap.Empty();
	bIsTransforming = false;
	CurrentMode = ETransformMode::None;
	CurrentAxis = None;
	bIsNumericInput = false;
	NumericBuffer.Empty();

	// Clear transform visualization lines
	ClearTransformVisualization();
}

void FBlend4RealInputProcessor::ApplyTransform(const FVector& Direction, const float Value, const bool InvertSnapState)
{
	if (!bIsTransforming || !GEditor)
	{
		return;
	}

	TransformSelectedActors(Direction, Value, true, InvertSnapState);
}

void FBlend4RealInputProcessor::ApplyNumericTransform()
{
	if (!bIsTransforming)
	{
		return;
	}

	const FVector AxisVector = GetAxisVector(CurrentAxis);
	if (NumericBuffer.IsEmpty())
	{
		TransformSelectedActors(AxisVector, 0, false);
		const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();
		ShowTransformInfo(FString::Printf(TEXT("0.0")), CursorPos);
		UpdateTransformVisualization();
		return;
	}

	const float NumericValue = FCString::Atof(*NumericBuffer);


	TransformSelectedActors(AxisVector, CurrentAxis == WorldZ ? -NumericValue : NumericValue, false);

	UE_LOG(LogTemp, Display, TEXT("Applied Numeric Transform: %f on axis %d"), NumericValue,
	       static_cast<int32>(CurrentAxis));

	UpdateTransformVisualization();
}

static const char* AxesLabels[] = {
	"None", "X", "Y", "Z", "Local X", "Local Y", "Local Z",
};

void FBlend4RealInputProcessor::SetTransformAxis(ETransformAxis Axis)
{
	if (CurrentAxis == Axis || CurrentMode == ETransformMode::Scale)
	{
		// to local axis
		// Scale is always forced in local space ( Unreal also does that with its gizmo)
		CurrentAxis = static_cast<ETransformAxis>(Axis + 3);
	}
	else
	{
		CurrentAxis = Axis;
	}

	const FString AxisText = AxesLabels[CurrentAxis];
	UE_LOG(LogTemp, Display, TEXT("Transform Axis Set: %s"), *AxisText);

	UpdateTransformVisualization();
}

FVector FBlend4RealInputProcessor::GetAxisVector(const ETransformAxis Axis) const
{
	switch (Axis)
	{
	case LocalX:
		if (ActorsTransformMap.Array().Num() == 1)
		{
			return ActorsTransformMap.Array()[0].Value.GetRotation().GetForwardVector();
		}
	// No break on purpose to fall back to worldX
	case WorldX:
		return FVector(1.0, 0.0, 0.0);

	case LocalY:
		if (ActorsTransformMap.Array().Num() == 1)
		{
			return ActorsTransformMap.Array()[0].Value.GetRotation().GetRightVector();
		}
	case WorldY:
		return FVector(0.0, 1.0, 0.0);

	case LocalZ:
		if (ActorsTransformMap.Array().Num() == 1)
		{
			return ActorsTransformMap.Array()[0].Value.GetRotation().GetUpVector();
		}
	case WorldZ:
		return FVector(0.0, 0.0, 1.0);

	case None:
	default:
		{
			if (CurrentMode == ETransformMode::Translation)
			{
				return FVector(0.0, 0.0, 0.0);
			}
			if (CurrentMode == ETransformMode::Rotation)
			{
				const auto* World = GetWorld();
				if (World == nullptr)
				{
					return FVector(0.0, 0.0, 0.0);
				}

				const auto& ViewLocations = World->ViewLocationsRenderedLastFrame;
				if (ViewLocations.Num() == 0)
				{
					return FVector(0.0, 0.0, 0.0);
				}

				const FVector CamLocation = ViewLocations[0];
				const FVector OutAxis = (CamLocation - TransformPivot.GetLocation()).GetSafeNormal();
				return OutAxis; // Default to direction from camera to object
			}
			// Scale
			return FVector(1.0, 1.0, 1.0); // Uniform scale
		}
	}
}

bool FBlend4RealInputProcessor::IsTransformKey(const FKeyEvent& KeyEvent)
{
	const FKey Key = KeyEvent.GetKey();
	return Key == EKeys::G || Key == EKeys::R || Key == EKeys::S;
}

bool FBlend4RealInputProcessor::IsAxisKey(const FKeyEvent& KeyEvent, ETransformAxis& OutAxis)
{
	const FKey Key = KeyEvent.GetKey();

	if (Key == EKeys::X)
	{
		OutAxis = WorldX;
		return true;
	}
	if (Key == EKeys::Y)
	{
		OutAxis = WorldY;
		return true;
	}
	if (Key == EKeys::Z)
	{
		OutAxis = WorldZ;
		return true;
	}

	return false;
}

bool FBlend4RealInputProcessor::IsNumericKey(const FKeyEvent& KeyEvent, FString& OutDigit)
{
	const FKey Key = KeyEvent.GetKey();

	// Check for number keys (0-9)
	if (Key == EKeys::Zero || Key == EKeys::NumPadZero)
	{
		OutDigit = TEXT("0");
		return true;
	}
	if (Key == EKeys::One || Key == EKeys::NumPadOne)
	{
		OutDigit = TEXT("1");
		return true;
	}
	if (Key == EKeys::Two || Key == EKeys::NumPadTwo)
	{
		OutDigit = TEXT("2");
		return true;
	}
	if (Key == EKeys::Three || Key == EKeys::NumPadThree)
	{
		OutDigit = TEXT("3");
		return true;
	}
	if (Key == EKeys::Four || Key == EKeys::NumPadFour)
	{
		OutDigit = TEXT("4");
		return true;
	}
	if (Key == EKeys::Five || Key == EKeys::NumPadFive)
	{
		OutDigit = TEXT("5");
		return true;
	}
	if (Key == EKeys::Six || Key == EKeys::NumPadSix)
	{
		OutDigit = TEXT("6");
		return true;
	}
	if (Key == EKeys::Seven || Key == EKeys::NumPadSeven)
	{
		OutDigit = TEXT("7");
		return true;
	}
	if (Key == EKeys::Eight || Key == EKeys::NumPadEight)
	{
		OutDigit = TEXT("8");
		return true;
	}
	if (Key == EKeys::Nine || Key == EKeys::NumPadNine)
	{
		OutDigit = TEXT("9");
		return true;
	}
	if (Key == EKeys::Nine || Key == EKeys::NumPadNine)
	{
		OutDigit = TEXT("9");
		return true;
	}
	if (Key == EKeys::Period || Key == EKeys::Decimal)
	{
		// Allow decimal point
		OutDigit = TEXT(".");
		return true;
	}
	if (Key == EKeys::Hyphen || Key == EKeys::Subtract)
	{
		// Allow negative numbers
		OutDigit = TEXT("-");
		return true;
	}
	return false;
}

void FBlend4RealInputProcessor::SetSelectionAsModified()
{
	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			Actor->Modify();
		}
	}
}

void FBlend4RealInputProcessor::ResetSelectedActorsTransform(const ETransformMode TransformMode)
{
	// reset selection translation
	GEditor->BeginTransaction(TEXT(""), FText::FromString(TEXT("Reset Rotation")), nullptr);
	SetSelectionAsModified();
	switch (TransformMode)
	{
	case ETransformMode::None:
		break;
	case ETransformMode::Translation:
		{
			const FVector Translation(0.0);
			SetDirectTransformToSelectedActor(&Translation);
		}
		break;
	case ETransformMode::Rotation:
		{
			const FRotator Rotation(0.0);
			SetDirectTransformToSelectedActor(nullptr, &Rotation);
		}
		break;
	case ETransformMode::Scale:
		{
			const FVector Scale(1.0);
			SetDirectTransformToSelectedActor(nullptr, nullptr, &Scale);
		}
		break;
	}
	GEditor->EndTransaction();
}

void FBlend4RealInputProcessor::SetDirectTransformToSelectedActor(const FVector* Location,
                                                                  const FRotator* Rotation,
                                                                  const FVector* Scale)
{
	// Apply the new transform to selected Actors
	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			FTransform ActorTransform = Actor->GetActorTransform();
			ActorTransform.SetLocation(Location ? *Location : ActorTransform.GetLocation());
			ActorTransform.SetRotation(Rotation ? Rotation->Quaternion() : ActorTransform.GetRotation());
			ActorTransform.SetScale3D(Scale ? *Scale : ActorTransform.GetScale3D());

			// Apply the new transform to the actor
			if (!ActorTransform.ContainsNaN())
			{
				Actor->SetActorTransform(ActorTransform, false, nullptr, ETeleportType::None);
			}
		}
	}
}

void FBlend4RealInputProcessor::TransformSelectedActors(const FVector& Direction, const float Value, const bool Snap,
                                                        const bool InvertSnap)
{
	if (!GEditor)
	{
		return;
	}

	// Get snap settings
	const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();
	const bool IsSnapTrEnabled = InvertSnap ? !ViewportSettings->GridEnabled : ViewportSettings->GridEnabled;
	const bool IsSnapRtEnabled = InvertSnap ? !ViewportSettings->RotGridEnabled : ViewportSettings->RotGridEnabled;
	const bool IsSnapScEnabled = InvertSnap ? !ViewportSettings->SnapScaleEnabled : ViewportSettings->SnapScaleEnabled;
	const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();
	FTransform NewTransform = TransformPivot;
	switch (CurrentMode)
	{
	case ETransformMode::Translation:
		{
			const float GridSize = GEditor->GetGridSize();
			const bool DoSnap = IsSnapTrEnabled && Snap;
			const FVector Translation = Direction * Value;

			const float SnappedValue = DoSnap ? (ceilf(Value / GridSize) * GridSize) : Value;
			const FVector SnappedTranslation = DoSnap
				                                   ? FVector(ceilf(Translation.X / GridSize) * GridSize,
				                                             ceilf(Translation.Y / GridSize) * GridSize,
				                                             ceilf(Translation.Z / GridSize) * GridSize)
				                                   : Translation;

			NewTransform.SetLocation(NewTransform.GetLocation() + SnappedTranslation);
			if (Value != 0.0)
			{
				ShowTransformInfo(FString::Printf(TEXT("%.1f"), SnappedValue), CursorPos);
			}
		}
		break;
	case ETransformMode::Rotation:
		{
			// TODO Snap
			if (Direction.IsNearlyZero())
			{
				NewTransform.SetRotation(TransformPivot.GetRotation());
				break;
			}
			const bool DoSnap = IsSnapRtEnabled && Snap;
			const float SnapAngle = GEditor->GetRotGridSize().Yaw;
			const float SnappedValue = DoSnap ? (ceilf(Value / SnapAngle) * SnapAngle) : Value;
			const FQuat DeltaRotation = FQuat(Direction, FMath::DegreesToRadians(-SnappedValue));
			NewTransform.SetRotation(DeltaRotation * TransformPivot.GetRotation());

			ShowTransformInfo(FString::Printf(TEXT("%.1f°"), CurrentAxis == WorldZ ? -SnappedValue : SnappedValue),
			                  CursorPos);
		}
		break;
	case ETransformMode::Scale:
		{
			if (Value == 0.0)
			{
				NewTransform.SetScale3D(TransformPivot.GetScale3D());
				break;
			}
			const bool DoSnap = IsSnapScEnabled && Snap;

			const float ScaleSnapValue = GEditor->GetScaleGridSize();
			const float SnappedValue = DoSnap ? (ceilf(Value / ScaleSnapValue) * ScaleSnapValue) : Value;
			NewTransform.SetScale3D(Direction * (SnappedValue - 1.0) + 1.0);
			ShowTransformInfo(FString::Printf(TEXT("x %.2f"), SnappedValue), CursorPos);
		}
		break;
	default:
		break;
	}


	// Apply the new transform to selected Actors
	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			// Get the current transform
			FTransform ActorTransform = ActorsTransformMap[Actor->GetUniqueID()];

			// cancel the pivot transform
			ActorTransform = ActorTransform * TransformPivot.Inverse();
			// multiply by the new transform
			ActorTransform = ActorTransform * NewTransform;

			// Apply the new transform to the actor
			if (!ActorTransform.ContainsNaN())
			{
				Actor->SetActorTransform(ActorTransform, false, nullptr, ETeleportType::None);
			}
		}
	}
	// Note: Viewport redraw is handled by UpdateTransformVisualization()
}

void FBlend4RealInputProcessor::ShowTransformInfo(const FString& Text, const FVector2D& ScreenPosition)
{
	if (!TransformInfoWindow.IsValid())
	{
		TransformInfoWindow = SNew(SWindow)
			.Type(EWindowType::CursorDecorator)
			.IsPopupWindow(true)
			.SizingRule(ESizingRule::Autosized)
			.SupportsTransparency(EWindowTransparency::PerWindow)
			.FocusWhenFirstShown(false)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(0, 0, 0, 0.4f))
				.Padding(FMargin(8, 4))
				[
					SAssignNew(TransformInfoText, STextBlock)
					.Text(FText::FromString(Text))
					.ColorAndOpacity(FLinearColor::White)
				]
			];

		FSlateApplication::Get().AddWindow(TransformInfoWindow.ToSharedRef());
	}
	// update Text
	TransformInfoText->SetText(FText::FromString(Text));
	// Update position (offset from cursor)
	TransformInfoWindow->MoveWindowTo(ScreenPosition + FVector2D(20, 20));
}

void FBlend4RealInputProcessor::HideTransformInfo()
{
	if (TransformInfoWindow.IsValid())
	{
		TransformInfoWindow->RequestDestroyWindow();
		TransformInfoWindow.Reset();
	}
}

void FBlend4RealInputProcessor::UpdateTransformVisualization()
{
	if (!bIsTransforming)
	{
		return;
	}

	// Get or cache the line batcher
	if (!LineBatcher)
	{
		const UWorld* World = GetWorld();
		if (!World)
		{
			return;
		}
		// Use WorldPersistent because World and Foreground get flushed after each viewport draw
		LineBatcher = World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent);
		if (!LineBatcher)
		{
			return;
		}
	}

	// Clear previous lines for this batch
	LineBatcher->ClearBatch(TRANSFORM_BATCH_ID);

	// Draw mode-specific visualization
	if (CurrentMode == ETransformMode::Rotation)
	{
		LineBatcher->DrawLine(
			TransformPivot.GetLocation(),
			HitLocation,
			FLinearColor::White, SDPG_Foreground, 1.0f, 0.0f, TRANSFORM_BATCH_ID);

		const FVector InitialDirection = (DragInitialProjectedPosition - TransformPivot.GetLocation()).GetSafeNormal();
		const FVector EndPos = InitialDirection * 100.0 + TransformPivot.GetLocation();
		LineBatcher->DrawLine(
			TransformPivot.GetLocation(),
			EndPos,
			FLinearColor(FColor::Cyan), SDPG_Foreground, 1.0f, 0.0f, TRANSFORM_BATCH_ID);
	}
	else if (CurrentMode == ETransformMode::Scale)
	{
		LineBatcher->DrawLine(
			TransformPivot.GetLocation(),
			HitLocation,
			FLinearColor::White, SDPG_Foreground, 1.0f, 0.0f, TRANSFORM_BATCH_ID);
	}

	// Draw axis constraint line if an axis is selected
	if (CurrentAxis != None)
	{
		const FVector DragInitialActorPosition = TransformPivot.GetLocation();
		const FVector Axis = GetAxisVector(CurrentAxis) * 100000.0;
		LineBatcher->DrawLine(
			DragInitialActorPosition - Axis,
			DragInitialActorPosition + Axis,
			FLinearColor(AxisColor[CurrentAxis]), SDPG_Foreground, 2.0f, 0.0f, TRANSFORM_BATCH_ID);
	}

	// Force viewport redraw - critical for non-realtime viewports
	GEditor->RedrawLevelEditingViewports();
}

void FBlend4RealInputProcessor::ClearTransformVisualization()
{
	if (LineBatcher)
	{
		LineBatcher->ClearBatch(TRANSFORM_BATCH_ID);
		UE_LOG(LogTemp, Display, TEXT("Clear"));
		GEditor->RedrawLevelEditingViewports();
	}
}
