#pragma once

#include "CoreMinimal.h"
#include "IBlend4RealTransformHandler.h"

class USplineComponent;

/**
 * Transform handler for spline control points.
 * Uses the spline visualizer's selection state to determine which points to transform.
 */
class FSplinePointTransformHandler : public IBlend4RealTransformHandler
{
public:
	FSplinePointTransformHandler(USplineComponent* InSplineComp, const TSet<int32>& InSelectedKeys);
	virtual ~FSplinePointTransformHandler() override = default;

	// === Selection Queries ===
	virtual bool HasSelection() const override;
	virtual int32 GetSelectionCount() const override;

	// === Transform Data ===
	virtual FTransform ComputeSelectionPivot() const override;
	virtual FTransform GetFirstSelectedItemTransform() const override;
	virtual FVector ComputeAverageLocalAxis(EAxis::Type Axis) const override;

	// === State Management ===
	virtual void CaptureInitialState() override;
	virtual void RestoreInitialState() override;

	// === Transform Application ===
	virtual void ApplyTransformAroundPivot(const FTransform& InitialPivot, const FTransform& NewPivotTransform) override;
	virtual void SetDirectTransform(const FVector* Location, const FRotator* Rotation, const FVector* Scale) override;

	// === Transaction Handling ===
	virtual int32 BeginTransaction(const FText& Description) override;
	virtual void EndTransaction() override;
	virtual void CancelTransaction(int32 TransactionIndex) override;

private:
	/** The spline component being edited */
	TWeakObjectPtr<USplineComponent> SplineComponent;

	/** Indices of selected control points */
	TSet<int32> SelectedPointIndices;

	/** Initial state for each control point (for cancel/restore) */
	struct FPointState
	{
		FVector Location;
		FQuat Rotation;
		FVector Scale;
		FVector ArriveTangent;
		FVector LeaveTangent;
	};
	TMap<int32, FPointState> InitialPointStates;
};
