#pragma once

#include "CoreMinimal.h"
#include "IBlend4RealTransformHandler.h"

/**
 * Transform handler for Level Editor actors.
 * Operates on GEditor->GetSelectedActors().
 */
class FActorTransformHandler : public IBlend4RealTransformHandler
{
public:
	FActorTransformHandler() = default;
	virtual ~FActorTransformHandler() override = default;

	// Selection Queries
	virtual bool HasSelection() const override;
	virtual int32 GetSelectionCount() const override;

	// Transform Data
	virtual FTransform ComputeSelectionPivot() const override;
	virtual FTransform GetFirstSelectedItemTransform() const override;
	virtual FVector ComputeAverageLocalAxis(EAxis::Type Axis) const override;

	// State Management
	virtual void CaptureInitialState() override;
	virtual void RestoreInitialState() override;

	// Transform Application
	virtual void
	ApplyTransformAroundPivot(const FTransform& InitialPivot, const FTransform& NewPivotTransform) override;
	virtual void SetDirectTransform(const FVector* Location, const FRotator* Rotation, const FVector* Scale) override;

	// Transaction Handling
	virtual int32 BeginTransaction(const FText& Description) override;
	virtual void EndTransaction() override;
	virtual void CancelTransaction(int32 TransactionIndex) override;

	/** Get the stored initial transform for an actor by its unique ID */
	const FTransform* GetInitialTransform(uint32 ActorUniqueID) const;

private:
	/** Stored initial transforms keyed by actor unique ID */
	TMap<uint32, FTransform> InitialTransforms;
};
