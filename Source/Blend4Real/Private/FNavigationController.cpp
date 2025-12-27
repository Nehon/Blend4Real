#include "FNavigationController.h"
#include "Blend4RealUtils.h"
#include "Blend4RealSettings.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Selection.h"

FNavigationController::FNavigationController()
{
}

FEditorViewportClient* FNavigationController::GetViewportClient() const
{
	return Blend4RealUtils::GetFocusedViewportClient();
}

void FNavigationController::BeginOrbit(const FVector2D& MousePosition)
{
	const FEditorViewportClient* ViewportClient = GetViewportClient();
	if (!ViewportClient || !ViewportClient->IsPerspective())
	{
		return;
	}

	bIsOrbiting = true;
	LastMousePosition = FSlateApplication::Get().GetCursorPos();

	// Default orbit pivot: use look-at location if valid, otherwise compute from camera
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
		
		// If no hit, keep the fallback pivot (already set above)
	}
	else if (Settings->ShouldOrbitAroundSelection() && SelectedActors && SelectedActors->Num() > 0)
	{
		// Override pivot with selection center if something is selected
		OrbitPivot = Blend4RealUtils::ComputeSelectionPivot().GetLocation();
	}

	//DrawDebugPoint(ViewportClient->GetWorld(),OrbitPivot,20,FColor::Red,false,10,10);
}

void FNavigationController::EndOrbit()
{
	bIsOrbiting = false;
}

void FNavigationController::BeginPan()
{
	const FEditorViewportClient* ViewportClient = GetViewportClient();
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
	FEditorViewportClient* ViewportClient = GetViewportClient();
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

	// Invalidate the viewport to trigger a redraw
	ViewportClient->Invalidate();
}

void FNavigationController::UpdatePan(const FVector2D& Delta) const
{
	FEditorViewportClient* ViewportClient = GetViewportClient();
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

	// Invalidate the viewport to trigger a redraw
	ViewportClient->Invalidate();
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

	FEditorViewportClient* ViewportClient = GetViewportClient();
	if (ViewportClient)
	{
		ViewportClient->FocusViewportOnBox(Bounds);
		return true;
	}

	return false;
}
