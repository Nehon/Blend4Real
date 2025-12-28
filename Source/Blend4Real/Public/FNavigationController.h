#pragma once

#include "CoreMinimal.h"

class FEditorViewportClient;

/**
 * Handles camera navigation operations: orbit, pan, and focus
 */
class FNavigationController
{
public:
	FNavigationController();

	/** Start orbiting around a pivot point */
	void BeginOrbit(const FVector2D& MousePosition);

	/** End orbit operation */
	void EndOrbit();

	/** Start panning the camera */
	void BeginPan(const FVector2D& MousePosition);

	/** End pan operation */
	void EndPan();

	/** Update orbit based on mouse delta */
	void UpdateOrbit(const FVector2D& Delta) const;

	/** Update pan based on mouse delta */
	void UpdatePan(const FVector2D& MousePosition);

	/** Focus viewport on the surface under the mouse cursor */
	bool FocusOnMouseHit(const FVector2D& MousePosition);

	/** Returns true if currently orbiting or panning */
	bool IsNavigating() const { return bIsOrbiting || bIsPanning; }

	/** Returns true if currently orbiting */
	bool IsOrbiting() const { return bIsOrbiting; }

	/** Returns true if currently panning */
	bool IsPanning() const { return bIsPanning; }

	/** Get the last recorded mouse position */
	FVector2D GetLastMousePosition() const { return LastMousePosition; }

	/** Set the last mouse position (called by input processor on mouse move) */
	void SetLastMousePosition(const FVector2D& Position) { LastMousePosition = Position; }

private:
	/** Get the viewport client - returns captured viewport during navigation, otherwise focused viewport */
	FEditorViewportClient* GetViewportClient() const;

	/** Update orbit for viewports in orbit camera mode (Material Editor, Niagara, etc.) */
	void UpdateOrbitCameraMode(FEditorViewportClient* ViewportClient, const FVector2D& Delta) const;

	/** Update orbit for viewports in regular camera mode (Level Editor) */
	void UpdateRegularCameraMode(FEditorViewportClient* ViewportClient, const FVector2D& Delta) const;

	/** Update pan for viewports in orbit camera mode */
	void UpdatePanOrbitCameraMode(FEditorViewportClient* ViewportClient, const FVector2D& MousePosition);

	bool bIsOrbiting = false;
	bool bIsPanning = false;
	bool bPlaneLessPan = false;
	bool bIsOrbitCameraMode = false;  // True if viewport uses orbit camera (Material Editor, Niagara, etc.)
	FEditorViewportClient* CapturedViewportClient = nullptr;  // Viewport captured at navigation start
	FVector OrbitPivot = FVector::ZeroVector;
	FVector PanPivot = FVector::ZeroVector;
	FVector RayOrigin, RayDirection;
	FVector StartPanCameraLocation = FVector::ZeroVector;
	FVector StartPanLookAtLocation = FVector::ZeroVector;  // For orbit camera mode pan
	FVector2D LastMousePosition = FVector2D::ZeroVector;
	FPlane PanPlane;

	FIntRect PanUnscaledViewRect;
	FMatrix PanInvViewProjectionMatrix;
};
