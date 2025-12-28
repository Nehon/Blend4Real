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
	// During navigation, use the captured viewport to prevent bleeding to other viewports
	if (CapturedViewportClient && (bIsOrbiting || bIsPanning))
	{
		return CapturedViewportClient;
	}
	return Blend4RealUtils::GetFocusedViewportClient();
}

void FNavigationController::BeginOrbit(const FVector2D& MousePosition)
{
	FEditorViewportClient* ViewportClient = GetViewportClient();
	if (!ViewportClient || !ViewportClient->IsPerspective())
	{
		return;
	}

	// Capture the viewport at navigation start to prevent bleeding to other viewports
	CapturedViewportClient = ViewportClient;
	bIsOrbiting = true;
	LastMousePosition = FSlateApplication::Get().GetCursorPos();

	// Detect if viewport is in orbit camera mode (Material Editor, Niagara, etc.)
	bIsOrbitCameraMode = ViewportClient->bUsingOrbitCamera;

	// Default orbit pivot: use look-at location if valid, otherwise compute from camera
	OrbitPivot = ViewportClient->GetLookAtLocation();

	// Get orbit mode from settings
	const UBlend4RealSettings* Settings = UBlend4RealSettings::Get();
	const USelection* SelectedActors = GEditor->GetSelectedActors();

	if (Settings->ShouldOrbitAroundMouseHit())
	{
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
	CapturedViewportClient = nullptr;
}

// For Pan we need quite some state information.
// The initial camera position : StartPanCameraLocation
// a camera aligned plane computed from the picked position in the scene: Pan Plane
// The Original camera Inverse view projection matrix: PanUnscaledViewRect
// The original unscaled view rectangle (even if unlikely to change during the transform): PanUnscaledViewRect
// the on pan update we compute the distance of the mouse projected on this plane, as if the projection matrix had not changed
// this gives us a perfect pan offset and the effect is that it feels like you are grabing and dragging the scene
// (the mouse cursor stays over the same point in the scene during the drag).
// The other benefit if this technique (over a classic constant * speed offset) is that the closer you are to the
// target the lower the pan speed will be in world space, allowing for precise panning.  
void FNavigationController::BeginPan(const FVector2D& MousePosition)
{
	FEditorViewportClient* ViewportClient = GetViewportClient();
	// if we fail to pick a position in the scene we can't compute the plane.
	// We fall back to a classic constant+speed panning
	bPlaneLessPan = false;
	if (!ViewportClient)
	{
		bPlaneLessPan = true;
		return;
	}

	// Capture the viewport at navigation start to prevent bleeding to other viewports
	CapturedViewportClient = ViewportClient;
	bIsPanning = true;
	LastMousePosition = FSlateApplication::Get().GetCursorPos();

	// Detect if viewport is in orbit camera mode
	bIsOrbitCameraMode = ViewportClient->bUsingOrbitCamera;

	const FSceneView* Scene = Blend4RealUtils::GetActiveSceneView(ViewportClient);
	if (!Scene)
	{
		bPlaneLessPan = true;
		return;
	}
	// pick in the scene.
	const FHitResult Result = Blend4RealUtils::ScenePickAtPosition(MousePosition, RayOrigin, RayDirection);
	PanPivot = OrbitPivot; // default to last OrbitPivot in case we don't hit anything
	if (Result.IsValidBlockingHit())
	{
		// use the picked position to construct the plane
		PanPivot = Result.Location;
	}

	// Save the start camera position.
	// In orbit camera mode, GetViewLocation() returns orbit parameters, not actual camera position.
	// We need to compute the actual camera position from the orbit matrix.
	if (bIsOrbitCameraMode)
	{
		FViewportCameraTransform& ViewTransform = ViewportClient->GetViewTransform();
		StartPanCameraLocation = ViewTransform.ComputeOrbitMatrix().Inverse().GetOrigin();
		StartPanLookAtLocation = ViewportClient->GetLookAtLocation();
	}
	else
	{
		StartPanCameraLocation = ViewportClient->GetViewLocation();
	}
	const FVector TransformViewDir = Scene->GetViewDirection().GetSafeNormal();

	// create the plane
	const FPlane ZeroPlane(TransformViewDir, 0.0);
	const float Dist = FMath::RayPlaneIntersectionParam(PanPivot, TransformViewDir, ZeroPlane);
	const FVector Normal = -TransformViewDir;
	PanPlane = FPlane(Normal, Dist);

	// uncomment to debug the plane position
	//DrawDebugSolidPlane(ViewportClient->GetWorld(), PanPlane, PanPivot, 500, FColor::Yellow, false, 5, 0);

	// save original POV informations
	PanInvViewProjectionMatrix = Blend4RealUtils::GetActiveSceneView(ViewportClient)->ViewMatrices.
		GetInvViewProjectionMatrix();
	PanUnscaledViewRect = Scene->UnscaledViewRect;
	Scene->DeprojectScreenToWorld(MousePosition, PanUnscaledViewRect, PanInvViewProjectionMatrix, RayOrigin,
	                              RayDirection);
	// reproject the Pan pivot origin on the plane.
	PanPivot = FMath::RayPlaneIntersection(RayOrigin, RayDirection, PanPlane);
}

void FNavigationController::EndPan()
{
	bIsPanning = false;
	CapturedViewportClient = nullptr;
}

void FNavigationController::UpdateOrbit(const FVector2D& Delta) const
{
	FEditorViewportClient* ViewportClient = GetViewportClient();
	if (!ViewportClient)
	{
		return;
	}

	if (bIsOrbitCameraMode)
	{
		UpdateOrbitCameraMode(ViewportClient, Delta);
	}
	else
	{
		UpdateRegularCameraMode(ViewportClient, Delta);
	}
}

void FNavigationController::UpdateOrbitCameraMode(FEditorViewportClient* ViewportClient, const FVector2D& Delta) const
{
	// In orbit camera mode, ViewLocation/ViewRotation store orbit parameters, not actual camera state.
	// We modify the rotation and then recompute the location from the orbit matrix.
	// This matches how UE handles orbit camera input in EditorViewportClient.cpp:2928-2936

	constexpr float RotationSpeed = 0.25f;
	const float DeltaYaw = -Delta.X * RotationSpeed;  // Inverted to match UE's orbit behavior
	const float DeltaPitch = -Delta.Y * RotationSpeed;

	FRotator CurrentRotation = ViewportClient->GetViewRotation();
	CurrentRotation.Yaw += DeltaYaw;
	CurrentRotation.Pitch = FMath::Clamp(CurrentRotation.Pitch + DeltaPitch, -89.0f, 89.0f);

	ViewportClient->SetViewRotation(CurrentRotation);

	// Recompute location from orbit matrix (this is the key step for orbit camera mode)
	FViewportCameraTransform& ViewTransform = ViewportClient->GetViewTransform();
	ViewportClient->SetViewLocation(ViewTransform.ComputeOrbitMatrix().Inverse().GetOrigin());

	ViewportClient->Invalidate();
}

void FNavigationController::UpdateRegularCameraMode(FEditorViewportClient* ViewportClient, const FVector2D& Delta) const
{
	// Regular camera mode: ViewLocation/ViewRotation are the actual camera state.
	// We orbit around our computed pivot point.

	constexpr float RotationSpeed = 0.25f;
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

	ViewportClient->Invalidate();
}

void FNavigationController::UpdatePan(const FVector2D& MousePosition)
{
	FEditorViewportClient* ViewportClient = GetViewportClient();
	if (!ViewportClient)
	{
		return;
	}

	if (bIsOrbitCameraMode)
	{
		UpdatePanOrbitCameraMode(ViewportClient, MousePosition);
		return;
	}

	if (bPlaneLessPan)
	{
		// if we failed to pick in the scene we don't have a plane, we fall back to this constant*speed offset
		// that's meh, but that's better than stopping the panning when we can't pick
		const FVector2D Delta = MousePosition - LastMousePosition;
		LastMousePosition = MousePosition;
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
		return;
	}
	LastMousePosition = MousePosition;
	// keep doing that in case we switch to the planeless panning in the middle of a drag
	// pick with new mouse pos but with start invViewProj
	Blend4RealUtils::GetActiveSceneView(ViewportClient)->DeprojectScreenToWorld(
		MousePosition, PanUnscaledViewRect, PanInvViewProjectionMatrix, RayOrigin,
		RayDirection);
	const FVector PlaneHit = FMath::RayPlaneIntersection(RayOrigin, RayDirection, PanPlane);
	// offset the original camera pos with the computed vector offset
	ViewportClient->SetViewLocation(StartPanCameraLocation - (PlaneHit - PanPivot));


	//DrawDebugLine(ViewportClient->GetWorld(), PanPivot, PlaneHit, FColor::Red, false, 0.1, 1);
	// Invalidate the viewport to trigger a redraw
	ViewportClient->Invalidate();
}

void FNavigationController::UpdatePanOrbitCameraMode(FEditorViewportClient* ViewportClient, const FVector2D& MousePosition)
{
	// In orbit camera mode, panning moves both the LookAt point and the camera together,
	// maintaining the orbit relationship. We use the same plane-based reprojection as regular mode
	// for precise, distance-aware panning.

	if (bPlaneLessPan)
	{
		// Fallback: simple delta-based panning when we couldn't create a plane
		const FVector2D Delta = MousePosition - LastMousePosition;
		LastMousePosition = MousePosition;

		if (Delta.IsNearlyZero())
		{
			return;
		}

		FViewportCameraTransform& ViewTransform = ViewportClient->GetViewTransform();
		const FRotator CameraRotation = ViewTransform.ComputeOrbitMatrix().InverseFast().Rotator();
		const FVector RightVector = FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Y);
		const FVector UpVector = FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Z);

		const FVector CurrentCameraLocation = ViewTransform.ComputeOrbitMatrix().Inverse().GetOrigin();
		const FVector LookAt = ViewportClient->GetLookAtLocation();
		const float DistanceToLookAt = FVector::Dist(CurrentCameraLocation, LookAt);
		const float PanSpeed = FMath::Max(DistanceToLookAt / 1000.0f, 0.1f);

		const FVector PanDelta = (-RightVector * Delta.X + UpVector * Delta.Y) * PanSpeed;
		ViewportClient->SetLookAtLocation(LookAt + PanDelta);
		ViewportClient->SetViewLocation(ViewTransform.ComputeOrbitMatrix().Inverse().GetOrigin());
		ViewportClient->Invalidate();
		return;
	}

	LastMousePosition = MousePosition;

	// Plane-based panning: deproject mouse using the ORIGINAL projection matrix from BeginPan
	// This ensures the mouse cursor stays over the same world point during the drag
	Blend4RealUtils::GetActiveSceneView(ViewportClient)->DeprojectScreenToWorld(
		MousePosition, PanUnscaledViewRect, PanInvViewProjectionMatrix, RayOrigin, RayDirection);
	const FVector PlaneHit = FMath::RayPlaneIntersection(RayOrigin, RayDirection, PanPlane);

	// Calculate the world-space offset from start
	const FVector Offset = PlaneHit - PanPivot;

	// Apply the same offset to the LookAt point (moves camera and lookat together)
	ViewportClient->SetLookAtLocation(StartPanLookAtLocation - Offset);

	// Recompute camera location from orbit matrix after moving LookAt
	FViewportCameraTransform& ViewTransform = ViewportClient->GetViewTransform();
	ViewportClient->SetViewLocation(ViewTransform.ComputeOrbitMatrix().Inverse().GetOrigin());

	ViewportClient->Invalidate();
}

bool FNavigationController::FocusOnMouseHit(const FVector2D& MousePosition)
{
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
