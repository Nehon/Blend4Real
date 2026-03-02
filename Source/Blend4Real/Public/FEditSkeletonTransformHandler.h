// Copyright 2025-Present - Nehon (Rémy Bouquet). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IBlend4RealTransformHandler.h"

class USkeletonEditingTool;

/**
 * Transform handler for bones in the Skeletal Mesh Editor's "Edit Skeleton" mode.
 *
 * Edit Skeleton mode uses a completely different transform pipeline from standard
 * Persona-based editors. It directly edits FReferenceSkeleton::RawRefBonePose[]
 * through USkeletonModifier, bypassing the animation system entirely.
 *
 * This handler goes through USkeletonEditingTool's public API:
 * - GetSelection() for bone selection
 * - GetTransform(BoneName, bWorld) for reading transforms
 * - SetTransforms(BoneNames, Transforms, bWorld) for writing transforms
 */
class FEditSkeletonTransformHandler : public IBlend4RealTransformHandler
{
public:
	FEditSkeletonTransformHandler(USkeletonEditingTool* InTool, UWorld* InPreviewWorld);
	virtual ~FEditSkeletonTransformHandler() override = default;

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
	struct FBoneState
	{
		FName BoneName;
		FTransform InitialWorldTransform;
		FTransform InitialLocalTransform;
	};

	USkeletonEditingTool* SkeletonTool;
	TWeakObjectPtr<UWorld> PreviewWorld;
	TArray<FBoneState> InitialStates;
};
