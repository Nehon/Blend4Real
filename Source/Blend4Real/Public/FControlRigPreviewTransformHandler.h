// Copyright 2025-Present - Nehon (Rémy Bouquet). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IBlend4RealTransformHandler.h"

class AControlRigShapeActor;
class UControlRig;
class FControlRigInteractionScope;

/**
 * Transform handler for Control Rig shape actors in preview scenes (e.g., Control Rig Editor).
 * Similar to FControlRigShapeTransformHandler but works with the preview world
 * instead of the level editor world, and skips sequencer-related ControlModified broadcast.
 */
class FControlRigPreviewTransformHandler : public IBlend4RealTransformHandler
{
public:
	explicit FControlRigPreviewTransformHandler(UWorld* InPreviewWorld);
	virtual ~FControlRigPreviewTransformHandler() override = default;

	// === Static Helpers ===

	/** Returns true if any selected shape actor exists in the given world */
	static bool HasSelectedShapeActors(UWorld* World);

	/** Collect all selected shape actors with valid rig pointers in the given world */
	static void GetSelectedShapeActors(UWorld* World, TArray<AControlRigShapeActor*>& OutShapeActors);

	// === IBlend4RealTransformHandler ===

	virtual bool HasSelection() const override;
	virtual int32 GetSelectionCount() const override;

	virtual FTransform ComputeSelectionPivot() const override;
	virtual FTransform GetFirstSelectedItemTransform() const override;
	virtual FVector ComputeAverageLocalAxis(EAxis::Type Axis) const override;

	virtual void CaptureInitialState() override;
	virtual void RestoreInitialState() override;

	virtual void ApplyTransformAroundPivot(
		const FTransform& InitialPivot,
		const FTransform& NewPivotTransform,
		TOptional<EAxis::Type> LocalScaleAxis = TOptional<EAxis::Type>()) override;
	virtual void SetDirectTransform(const FVector* Location, const FRotator* Rotation, const FVector* Scale) override;

	virtual int32 BeginTransaction(const FText& Description) override;
	virtual void EndTransaction() override;
	virtual void CancelTransaction(int32 TransactionIndex) override;

	virtual UWorld* GetVisualizationWorld() const override;

private:
	/** The preview world this handler operates in */
	TWeakObjectPtr<UWorld> PreviewWorld;

	/** Per-control initial state for capture/restore */
	struct FControlState
	{
		TWeakObjectPtr<AControlRigShapeActor> ShapeActor;
		TWeakObjectPtr<UControlRig> ControlRig;
		FName ControlName;
		FTransform WorldTransform;
	};

	TArray<FControlState> InitialStates;

	/** Interaction scopes that prevent the rig from overwriting control transforms during manipulation */
	TArray<TSharedPtr<FControlRigInteractionScope>> InteractionScopes;

	// === Space Conversion Helpers ===

	static FTransform GetComponentToWorld(const AControlRigShapeActor* ShapeActor);
	static FTransform GetControlWorldTransform(const AControlRigShapeActor* ShapeActor);
	static void SetControlWorldTransform(const AControlRigShapeActor* ShapeActor, const FTransform& WorldTransform, bool bSetupUndo);
};
