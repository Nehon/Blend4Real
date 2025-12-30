#pragma once

#include "CoreMinimal.h"
#include "IBlend4RealTransformHandler.h"

class USceneComponent;
class USelection;

/**
 * Transform handler for components selected in the Level Editor.
 * Uses GEditor->GetSelectedComponents() for selection.
 */
class FComponentTransformHandler : public IBlend4RealTransformHandler
{
public:
	FComponentTransformHandler() = default;
	virtual ~FComponentTransformHandler() override = default;

	// === Selection Queries ===
	virtual bool HasSelection() const override;
	virtual int32 GetSelectionCount() const override;

	// === Transform Data ===
	virtual FTransform ComputeSelectionPivot() const override;
	virtual FTransform GetFirstSelectedItemTransform() const override;

	// === State Management ===
	virtual void CaptureInitialState() override;
	virtual void RestoreInitialState() override;

	// === Transform Application ===
	virtual void
	ApplyTransformAroundPivot(const FTransform& InitialPivot, const FTransform& NewPivotTransform) override;
	virtual void SetDirectTransform(const FVector* Location, const FRotator* Rotation, const FVector* Scale) override;

	// === Transaction Handling ===
	virtual int32 BeginTransaction(const FText& Description) override;
	virtual void EndTransaction() override;
	virtual void CancelTransaction(int32 TransactionIndex) override;

private:
	/** Get the component selection from GEditor */
	USelection* GetSelectedComponents() const;

	/** Stored initial transforms keyed by component unique ID */
	TMap<uint32, FTransform> InitialTransforms;
};
