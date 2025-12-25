#include "FNavigationController.h"
#include "Blend4RealUtils.h"
#include "Blend4RealSettings.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Selection.h"

FNavigationController::FNavigationController()
{
}

FLevelEditorViewportClient* FNavigationController::GetViewportClient() const
{
	if (!GEditor)
	{
		return nullptr;
	}

	const FViewport* Viewport = GEditor->GetActiveViewport();
	if (!Viewport)
	{
		return nullptr;
	}

	return static_cast<FLevelEditorViewportClient*>(Viewport->GetClient());
}

void FNavigationController::BeginOrbit(const FVector2D& MousePosition)
{
	const FLevelEditorViewportClient* ViewportClient = GetViewportClient();
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
		FVector RayOrigin, RayDirection;
		const FHitResult Result = Blend4RealUtils::ScenePickAtPosition(MousePosition, RayOrigin, RayDirection);
		if (Result.IsValidBlockingHit())
		{
			OrbitPivot = Result.Location;
		}
	}
	else if (Settings->ShouldOrbitAroundSelection() && SelectedActors && SelectedActors->Num() > 0)
	{
		// Override pivot with selection center if something is selected
		OrbitPivot = Blend4RealUtils::ComputeSelectionPivot().GetLocation();
	}
}

void FNavigationController::EndOrbit()
{
	bIsOrbiting = false;
}

void FNavigationController::BeginPan()
{
	const FLevelEditorViewportClient* ViewportClient = GetViewportClient();
	if (!ViewportClient)
	{
		return;
	}

	bIsPanning = true;
	LastMousePosition = FSlateApplication::Get().GetCursorPos();
}

void FNavigationController::EndPan()
{
	bIsPanning = false;
}

void FNavigationController::UpdateOrbit(const FVector2D& Delta) const
{
	FLevelEditorViewportClient* ViewportClient = GetViewportClient();
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
	const FQuat PitchQuat = FQuat(RightVector, FMath::DegreesToRadians(ClampedPitch));

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

void FNavigationController::UpdatePan(const FVector2D& Delta) const
{
	FLevelEditorViewportClient* ViewportClient = GetViewportClient();
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

bool FNavigationController::FocusOnMouseHit(const FVector2D& MousePosition) const
{
	FVector RayOrigin, RayDirection;
	const FHitResult Result = Blend4RealUtils::ScenePickAtPosition(MousePosition, RayOrigin, RayDirection);

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

	FLevelEditorViewportClient* ViewportClient = GetViewportClient();
	if (ViewportClient)
	{
		ViewportClient->FocusViewportOnBox(Bounds);
		return true;
	}

	return false;
}
