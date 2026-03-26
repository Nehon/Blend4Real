// Copyright 2025-Present - Nehon (Rémy Bouquet). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IBlend4RealTransformHandler.h"

class FIKRetargetEditorController;
class UIKRetargeterController;
class UDebugSkelMeshComponent;
enum class ERetargetSourceOrTarget : uint8;

/**
 * Transform handler for bones in the IK Retargeter Editor's "Edit Retarget Pose" mode.
 *
 * The IK Retargeter uses a completely different architecture from Persona-based editors:
 * - Bone selection is managed by FIKRetargetEditorController (not IPersonaPreviewScene)
 * - Transforms are stored as rotation offsets per bone (not additive animation)
 * - Only the root/pelvis bone supports translation; all bones support rotation
 * - Scale is not applicable to retarget poses
 *
 * Access chain: FIKRetargetEditor::GetController() → FIKRetargetEditorController
 * Transform API: UIKRetargeterController (exported, SetRotationOffsetForRetargetPoseBone, etc.)
 */
class FRetargetBoneTransformHandler : public IBlend4RealTransformHandler
{
public:
	explicit FRetargetBoneTransformHandler(TSharedPtr<FIKRetargetEditorController> InController);
	virtual ~FRetargetBoneTransformHandler() override = default;

	// === IBlend4RealTransformHandler ===

	virtual bool SupportsMode(ETransformMode Mode) const override { return Mode == ETransformMode::Rotation || Mode == ETransformMode::Trackball; }
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
	/** Get the skeletal mesh component for the currently edited source/target */
	UDebugSkelMeshComponent* GetEditedMeshComponent() const;

	/** Get the bone's world-space transform from the retarget pose (with scale/offset applied) */
	FTransform GetBoneWorldTransform(const FName& BoneName) const;

	struct FBoneState
	{
		FName BoneName;
		FTransform InitialWorldTransform;
		FQuat InitialRotationOffset;
	};

	TSharedPtr<FIKRetargetEditorController> EditorController;
	TArray<FBoneState> InitialStates;
	FVector InitialRootTranslationOffset;
	FName PelvisBoneName;
	bool bRootSelected = false;
};
