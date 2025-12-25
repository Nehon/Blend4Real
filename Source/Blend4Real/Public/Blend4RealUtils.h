#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class FSceneView;
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
		TransformAxes_Count
	};
};

/**
 * Transform mode for object manipulation
 */
enum class ETransformMode
{
	None,
	Translation,  // 'G' key
	Rotation,     // 'R' key
	Scale         // 'S' key
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
	FSceneView* GetActiveSceneView();

	/** Compute the center pivot point of all selected actors */
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
	FHitResult ProjectToSurface(const FVector& Start, const FVector& Direction, const FCollisionQueryParams& Params);

	/** Check if the key event is a transform key (G/R/S) */
	bool IsTransformKey(const FKeyEvent& KeyEvent);

	/**
	 * Check if the key event is an axis key (X/Y/Z)
	 * @param KeyEvent - The key event to check
	 * @param OutAxis - Output axis if key matches
	 * @return True if key is an axis key
	 */
	bool IsAxisKey(const FKeyEvent& KeyEvent, ETransformAxis::Type& OutAxis);

	/**
	 * Check if the key event is a numeric key (0-9, period, minus)
	 * @param KeyEvent - The key event to check
	 * @param OutDigit - Output digit string if key matches
	 * @return True if key is a numeric key
	 */
	bool IsNumericKey(const FKeyEvent& KeyEvent, FString& OutDigit);

	/** Mark all selected actors as modified for undo system */
	void MarkSelectionModified();
}
