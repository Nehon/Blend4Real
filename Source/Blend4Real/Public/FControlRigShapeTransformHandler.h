// Copyright 2025-Present - Nehon (Rémy Bouquet). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IBlend4RealTransformHandler.h"

class AControlRigShapeActor;
class UControlRig;

/**
 * Transform handler for Control Rig shape actors.
 * Routes transforms through UControlRig::SetControlGlobalTransform()
 * instead of AActor::SetActorTransform(), so the underlying rig updates correctly.
 */
class FControlRigShapeTransformHandler : public IBlend4RealTransformHandler
{
public:
	FControlRigShapeTransformHandler() = default;
	virtual ~FControlRigShapeTransformHandler() override = default;

	// === Static Helpers (used by FTransformHandlerFactory) ===

	/** Returns true if any selected actor is an AControlRigShapeActor with a valid rig */
	static bool HasSelectedShapeActors();

	/** Collect all selected shape actors that have valid rig pointers */
	static void GetSelectedShapeActors(TArray<AControlRigShapeActor*>& OutShapeActors);

	// === IBlend4RealTransformHandler ===

	virtual bool HasSelection() const override;
	virtual int32 GetSelectionCount() const override;

	virtual FTransform ComputeSelectionPivot() const override;
	virtual FTransform GetFirstSelectedItemTransform() const override;
	virtual FVector ComputeAverageLocalAxis(const EAxis::Type Axis) const override;

	virtual void CaptureInitialState() override;
	virtual void RestoreInitialState() override;

	virtual void ApplyTransformAroundPivot(
		const FTransform& InitialPivot,
		const FTransform& NewPivotTransform,
		TOptional<EAxis::Type> LocalScaleAxis = TOptional<EAxis::Type>()) override;
	virtual void SetDirectTransform(const FVector* Location, const FRotator* Rotation, const FVector* Scale) override;

	virtual int32 BeginTransaction(const FText& Description) override;
	virtual void EndTransaction() override;
	virtual void CancelTransaction(const int32 TransactionIndex) override;

private:
	/** Per-control initial state for capture/restore */
	struct FControlState
	{
		TWeakObjectPtr<AControlRigShapeActor> ShapeActor;
		TWeakObjectPtr<UControlRig> ControlRig;
		FName ControlName;
		FTransform WorldTransform;
	};

	TArray<FControlState> InitialStates;

	// === Space Conversion Helpers ===

	/** Get the world transform of the skeletal mesh component bound to this rig */
	static FTransform GetComponentToWorld(const AControlRigShapeActor* ShapeActor);

	/** Get the control's transform in world space */
	static FTransform GetControlWorldTransform(const AControlRigShapeActor* ShapeActor);

	/** Set the control's transform from a world-space transform */
	static void SetControlWorldTransform(const AControlRigShapeActor* ShapeActor, const FTransform& WorldTransform, const bool bSetupUndo);
};
