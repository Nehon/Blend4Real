#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class FSceneView;
class FEditorViewportClient;
struct FHitResult;
struct FCollisionQueryParams;
struct FKeyEvent;

/**
 * Transform axis enumeration for constraint operations
 */
namespace ETransformAxis
{
	enum Type
	{
		None,
		WorldX,
		WorldY,
		WorldZ,
		LocalX,
		LocalY,
		LocalZ,
		WorldXPlane, // A plane representer by (XAxis, 0): movement constrained in both Y and Z axis. 
		WorldYPlane, // A plane representer by (YAxis, 0): movement constrained in both X and Z axis. 
		WorldZPlane, // A plane representer by (ZAxis, 0): movement constrained in both X and Y axis.
		LocalXPlane, // Same as WorldXPlane but local to the selection
		LocalYPlane, // Same as WorldYPlane but local to the selection
		LocalZPlane, // Same as WorldZPlane but local to the selection
		TransformAxes_Count
	};
};

/**
 * Transform mode for object manipulation
 */
enum class ETransformMode
{
	None,
	Translation, // 'G' key
	Rotation, // 'R' key
	Scale // 'S' key
};

/**
 * Stateless utility functions for Blend4Real plugin
 */
namespace Blend4RealUtils
{
	/** Axis colors for visualization (indexed by ETransformAxis) */
	extern const FColor AxisColors[ETransformAxis::TransformAxes_Count];

	/** Axis labels for debug output (indexed by ETransformAxis) */
	extern const char* AxisLabels[ETransformAxis::TransformAxes_Count];

	/** Get the editor world from the active viewport */
	UWorld* GetEditorWorld();

	/** Get the active scene view for raycasting */
	FSceneView* GetActiveSceneView(FEditorViewportClient* EClient = nullptr);

	/** Compute the center pivot point of all selected actors or components*/
	FTransform ComputeSelectionPivot();

	/**
	 * Perform a scene pick (raycast) at the given mouse position
	 * @param MousePosition - Screen space position to pick from
	 * @param OutRayOrigin - Output ray origin in world space
	 * @param OutRayDirection - Output ray direction in world space
	 * @return Hit result from the scene pick, invalid if nothing hit or mouse outside viewport
	 */
	FHitResult ScenePickAtPosition(const FVector2D& MousePosition, FVector& OutRayOrigin, FVector& OutRayDirection);

	/**
	 * Project a ray onto scene surfaces
	 * @param Start - Ray start position
	 * @param Direction - Ray direction (should be normalized)
	 * @param Params - Collision query parameters
	 * @return Hit result from the trace
	 */
	FHitResult ProjectToSurface(const UWorld* World, const FVector& Start, const FVector& Direction,
	                            const FCollisionQueryParams& Params);

	/** Check if the key event is a transform key (G/R/S) */
	bool IsTransformKey(const FKeyEvent& KeyEvent);

	/**
	 * Check if the key event is an axis key (X/Y/Z)
	 * @param KeyEvent - The key event to check
	 * @param OutAxis - Output axis if key matches
	 * @return True if key is an axis key
	 */
	bool IsAxisKey(const FKeyEvent& KeyEvent, const EModifierKey::Type Modifiers, ETransformAxis::Type& OutAxis);

	/**
	 * Check if the key event is a numeric key (0-9, period, minus)
	 * @param KeyEvent - The key event to check
	 * @param OutDigit - Output digit string if key matches
	 * @return True if key is a numeric key
	 */
	bool IsNumericKey(const FKeyEvent& KeyEvent, FString& OutDigit);

	/** Mark all selected actors as modified for undo system */
	void MarkSelectionModified();

	/**
	 * Check if an editor viewport widget currently has keyboard focus
	 * @return True if any editor viewport (level, asset, etc.) has focus
	 */
	bool IsEditorViewportWidgetFocused();

	/**
	 * Get the viewport client for the active editor viewport
	 * Uses GEditor->GetActiveViewport() for reliability
	 * @return The viewport client, or nullptr if not available
	 */
	FEditorViewportClient* GetFocusedViewportClient();

	/**
	 * Check if the currently focused viewport is a Level Editor viewport
	 * @return True if a Level Editor viewport has focus
	 */
	bool IsLevelEditorViewportFocused();

	/**
	 * Check if the currently focused viewport is a Blueprint SCS Editor viewport
	 * @return True if a Blueprint SCS Editor viewport has focus
	 */
	bool IsSCSEditorViewportFocused();

	/**
	 * Check if the mouse cursor is over an editor viewport
	 * @param MousePosition - Screen space position to check
	 * @param ViewportTypeFilter - Optional: specific viewport type to match (e.g., "SLevelViewport").
	 *                             If NAME_None, matches any editor viewport type.
	 * @return True if mouse is over a matching editor viewport
	 */
	bool IsMouseOverViewport(const FVector2D& MousePosition, const FName& ViewportTypeFilter = NAME_None);

	/**
	 * Get the viewport client at a screen position along with the viewport's screen origin
	 * @param ScreenPosition - Screen space position to check
	 * @param OutViewportScreenOrigin - Output: the viewport's top-left corner in screen space
	 * @param ViewportTypeFilter - Optional: specific viewport type to match (e.g., "SLevelViewport").
	 *                             If NAME_None, matches any editor viewport type.
	 * @return The viewport client, or nullptr if not over a matching editor viewport
	 */
	FEditorViewportClient* GetViewportClientAndScreenOrigin(const FVector2D& ScreenPosition,
	                                                        FVector2D& OutViewportScreenOrigin,
	                                                        const FName& ViewportTypeFilter = NAME_None);

	/** Get the 3D hit point on a plane from mouse position */
	FVector GetPlaneHit(const FVector& Normal, float Distance, FVector& RayOrigin, FVector& RayDirection);
}
