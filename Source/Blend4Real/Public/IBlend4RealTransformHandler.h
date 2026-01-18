#pragma once

#include "CoreMinimal.h"
#include "Blend4RealUtils.h"

/**
 * Abstract interface for transform handlers.
 *
 * Handlers are thin adapters that know how to:
 * - Query selection state for a specific viewport/context
 * - Convert selection transforms to/from FTransform
 * - Apply pre-computed transform deltas
 * - Handle undo/redo transactions
 *
 * All picking logic (plane computation, ray intersection, delta calculation)
 * stays in FTransformController - handlers just apply the results.
 */
class IBlend4RealTransformHandler
{
public:
	virtual ~IBlend4RealTransformHandler() = default;

	// === Selection Queries ===

	/** Returns true if there are items selected that can be transformed */
	virtual bool HasSelection() const = 0;

	/** Returns the number of selected items */
	virtual int32 GetSelectionCount() const = 0;

	// === Transform Data ===

	/** Returns the pivot point for multi-selection transforms (center of selection) */
	virtual FTransform ComputeSelectionPivot() const = 0;

	/**
	 * Returns the transform of the first selected item.
	 * Used for local axis computation when GetSelectionCount() == 1.
	 */
	virtual FTransform GetFirstSelectedItemTransform() const = 0;

	/**
	 * Computes the average local axis direction across all selected items.
	 * For each selected item, extracts the requested axis from its rotation,
	 * then averages all those direction vectors.
	 *
	 * @param Axis - Which axis to compute (X=Forward, Y=Right, Z=Up)
	 * @return The averaged and normalized axis direction vector
	 */
	virtual FVector ComputeAverageLocalAxis(EAxis::Type Axis) const = 0;

	// === State Management (for cancel) ===

	/** Capture initial transforms of all selected items */
	virtual void CaptureInitialState() = 0;

	/** Restore all selected items to their initial transforms (for cancel) */
	virtual void RestoreInitialState() = 0;

	// === Transform Application ===

	/**
	 * Apply pre-computed transform to selection.
	 * The delta values are already computed by FTransformController (with snapping, etc.)
	 *
	 * For Translation: NewPos = InitialPos * PivotInverse * (Pivot + DeltaTranslation)
	 * For Rotation: NewRot = DeltaRotation * InitialRot (around pivot)
	 * For Scale: NewScale = InitialScale * DeltaScale (around pivot)
	 *
	 * @param NewPivotTransform - The new pivot transform (includes translation/rotation/scale delta)
	 */
	virtual void ApplyTransformAroundPivot(const FTransform& InitialPivot, const FTransform& NewPivotTransform) = 0;

	/**
	 * Set absolute transform values on selected items.
	 * Used for ResetTransform operations.
	 * Pass nullptr to keep the existing value.
	 */
	virtual void SetDirectTransform(const FVector* Location, const FRotator* Rotation, const FVector* Scale) = 0;

	// === Transaction Handling (Undo/Redo) ===

	/** Begin an undo transaction with the given description */
	virtual int32 BeginTransaction(const FText& Description) = 0;

	/** End the current transaction (commit changes) */
	virtual void EndTransaction() = 0;

	/** Cancel the current transaction (discard changes) */
	virtual void CancelTransaction(int32 TransactionIndex) = 0;

	// === Visualization Context ===

	/**
	 * Get the world to use for visualization (axis lines, etc.)
	 * Returns nullptr to use the default editor world.
	 * Override in handlers that operate in preview scenes (e.g., SCS editor).
	 */
	virtual UWorld* GetVisualizationWorld() const { return nullptr; }
};
