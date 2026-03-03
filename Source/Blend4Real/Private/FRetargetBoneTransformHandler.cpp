// Copyright 2025-Present - Nehon (Rémy Bouquet). All Rights Reserved.

#include "FRetargetBoneTransformHandler.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Editor.h"

// ============================================================================
// Construction
// ============================================================================

FRetargetBoneTransformHandler::FRetargetBoneTransformHandler(
	TSharedPtr<FIKRetargetEditorController> InController)
	: EditorController(MoveTemp(InController))
{
	const ERetargetSourceOrTarget SourceOrTarget = EditorController->GetSourceOrTarget();
	PelvisBoneName = EditorController->AssetController->GetPelvisBone(SourceOrTarget);
}

// ============================================================================
// Helpers
// ============================================================================

UDebugSkelMeshComponent* FRetargetBoneTransformHandler::GetEditedMeshComponent() const
{
	if (!EditorController)
	{
		return nullptr;
	}

	// Use the public fields directly (GetSkeletalMeshComponent is not exported)
	return EditorController->GetSourceOrTarget() == ERetargetSourceOrTarget::Source
		? EditorController->SourceSkelMeshComponent
		: EditorController->TargetSkelMeshComponent;
}

FTransform FRetargetBoneTransformHandler::GetBoneWorldTransform(const FName& BoneName) const
{
	// Use the skeletal mesh component's bone transform directly.
	// The anim instance already applies the retarget pose, so GetBoneTransform()
	// returns the correct world-space position (with scale/offset baked in).
	UDebugSkelMeshComponent* MeshComp = GetEditedMeshComponent();
	if (!MeshComp || !MeshComp->GetSkeletalMeshAsset())
	{
		return FTransform::Identity;
	}

	const int32 BoneIndex = MeshComp->GetBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return FTransform::Identity;
	}

	return MeshComp->GetBoneTransform(BoneIndex);
}

// ============================================================================
// Selection Queries
// ============================================================================

bool FRetargetBoneTransformHandler::HasSelection() const
{
	return EditorController && !EditorController->GetSelectedBones().IsEmpty();
}

int32 FRetargetBoneTransformHandler::GetSelectionCount() const
{
	return EditorController ? EditorController->GetSelectedBones().Num() : 0;
}

// ============================================================================
// Transform Data
// ============================================================================

FTransform FRetargetBoneTransformHandler::ComputeSelectionPivot() const
{
	if (!EditorController)
	{
		return FTransform::Identity;
	}

	const TArray<FName>& SelectedBones = EditorController->GetSelectedBones();
	if (SelectedBones.IsEmpty())
	{
		return FTransform::Identity;
	}

	// Pivot at the last selected bone (matches the retargeter's own gizmo behavior)
	return GetBoneWorldTransform(SelectedBones.Last());
}

FTransform FRetargetBoneTransformHandler::GetFirstSelectedItemTransform() const
{
	if (InitialStates.Num() > 0)
	{
		return InitialStates[0].InitialWorldTransform;
	}

	return ComputeSelectionPivot();
}

FVector FRetargetBoneTransformHandler::ComputeAverageLocalAxis(const EAxis::Type Axis) const
{
	if (InitialStates.Num() == 0)
	{
		return FVector::ZeroVector;
	}

	FVector AccumulatedAxis = FVector::ZeroVector;

	for (const FBoneState& State : InitialStates)
	{
		const FQuat Rotation = State.InitialWorldTransform.GetRotation();
		FVector AxisVector;

		switch (Axis)
		{
		case EAxis::X:
			AxisVector = Rotation.GetForwardVector();
			break;
		case EAxis::Y:
			AxisVector = Rotation.GetRightVector();
			break;
		case EAxis::Z:
			AxisVector = Rotation.GetUpVector();
			break;
		default:
			AxisVector = FVector::ZeroVector;
			break;
		}

		AccumulatedAxis += AxisVector;
	}

	return (AccumulatedAxis / InitialStates.Num()).GetSafeNormal();
}

// ============================================================================
// State Management
// ============================================================================

void FRetargetBoneTransformHandler::CaptureInitialState()
{
	InitialStates.Empty();
	bRootSelected = false;

	if (!EditorController || !EditorController->AssetController)
	{
		return;
	}

	const ERetargetSourceOrTarget SourceOrTarget = EditorController->GetSourceOrTarget();
	const TArray<FName>& SelectedBones = EditorController->GetSelectedBones();

	for (const FName& BoneName : SelectedBones)
	{
		FBoneState State;
		State.BoneName = BoneName;
		State.InitialWorldTransform = GetBoneWorldTransform(BoneName);
		State.InitialRotationOffset = EditorController->AssetController->GetRotationOffsetForRetargetPoseBone(
			BoneName, SourceOrTarget);
		InitialStates.Add(MoveTemp(State));

		if (BoneName == PelvisBoneName)
		{
			bRootSelected = true;
		}
	}

	// Capture root translation offset
	if (bRootSelected)
	{
		InitialRootTranslationOffset = EditorController->AssetController->GetRootOffsetInRetargetPose(SourceOrTarget);
	}
}

void FRetargetBoneTransformHandler::RestoreInitialState()
{
	if (!EditorController || !EditorController->AssetController || InitialStates.Num() == 0)
	{
		return;
	}

	const ERetargetSourceOrTarget SourceOrTarget = EditorController->GetSourceOrTarget();

	for (const FBoneState& State : InitialStates)
	{
		EditorController->AssetController->SetRotationOffsetForRetargetPoseBone(
			State.BoneName, State.InitialRotationOffset, SourceOrTarget);
	}

	if (bRootSelected)
	{
		EditorController->AssetController->GetCurrentRetargetPose(SourceOrTarget)
			.SetRootTranslationDelta(InitialRootTranslationOffset);
	}
}

// ============================================================================
// Transform Application
// ============================================================================

void FRetargetBoneTransformHandler::ApplyTransformAroundPivot(
	const FTransform& InitialPivot,
	const FTransform& NewPivotTransform,
	TOptional<EAxis::Type> LocalScaleAxis)
{
	if (!EditorController || !EditorController->AssetController || InitialStates.Num() == 0)
	{
		return;
	}

	// Scale is not applicable to retarget poses — ignore entirely
	// (LocalScaleAxis set means scale mode, which is a no-op here)

	const ERetargetSourceOrTarget SourceOrTarget = EditorController->GetSourceOrTarget();

	// Compute rotation delta from pivot change
	const FQuat DeltaRotation = NewPivotTransform.GetRotation() * InitialPivot.GetRotation().Inverse();
	const FVector DeltaTranslation = NewPivotTransform.GetLocation() - InitialPivot.GetLocation();

	// Apply rotation to all selected bones
	for (const FBoneState& State : InitialStates)
	{
		// Convert world-space rotation delta to bone-local space
		// This follows the same pattern as FIKRetargetEditPoseMode::InputDelta:
		// 1. Get the rotation axis in world space
		// 2. Transform to bone-local space via InverseTransformVector
		// 3. Construct a bone-local quaternion from the local axis and angle
		const FVector RotationAxis = DeltaRotation.GetRotationAxis();
		const float RotationAngle = DeltaRotation.GetAngle();

		if (RotationAngle > KINDA_SMALL_NUMBER)
		{
			const FVector BoneLocalAxis = State.InitialWorldTransform.InverseTransformVector(RotationAxis);
			const FQuat BoneLocalDelta = FQuat(BoneLocalAxis, RotationAngle);

			// Combine with the bone's initial rotation offset
			const FQuat NewOffset = State.InitialRotationOffset * BoneLocalDelta;
			EditorController->AssetController->SetRotationOffsetForRetargetPoseBone(
				State.BoneName, NewOffset, SourceOrTarget);
		}
	}

	// Apply translation to root bone (if selected)
	if (bRootSelected && !DeltaTranslation.IsNearlyZero())
	{
		// Scale the translation delta by inverse of component scale (same as the retargeter's InputDelta)
		const UIKRetargeter* Asset = EditorController->AssetController->GetAsset();
		float ComponentScale = 1.0f;
		if (Asset)
		{
			const bool bIsSource = SourceOrTarget == ERetargetSourceOrTarget::Source;
			ComponentScale = bIsSource ? 1.0f : Asset->TargetMeshScale;
		}
		ComponentScale = FMath::Max(ComponentScale, UE_KINDA_SMALL_NUMBER);

		const FVector ScaledDelta = DeltaTranslation * (1.0f / ComponentScale);
		const FVector NewRootOffset = InitialRootTranslationOffset + ScaledDelta;

		// Use SetRootTranslationDelta directly on the pose (absolute set, not additive)
		EditorController->AssetController->GetCurrentRetargetPose(SourceOrTarget)
			.SetRootTranslationDelta(NewRootOffset);
	}
}

void FRetargetBoneTransformHandler::SetDirectTransform(
	const FVector* Location,
	const FRotator* Rotation,
	const FVector* Scale)
{
	if (!EditorController || !EditorController->AssetController || InitialStates.Num() == 0)
	{
		return;
	}

	const ERetargetSourceOrTarget SourceOrTarget = EditorController->GetSourceOrTarget();

	if (Rotation)
	{
		// Convert absolute world rotation to bone-local offset
		for (const FBoneState& State : InitialStates)
		{
			const FQuat DesiredWorldRotation = Rotation->Quaternion();
			const FQuat CurrentWorldRotation = State.InitialWorldTransform.GetRotation();

			// Compute the world-space delta needed
			const FQuat WorldDelta = DesiredWorldRotation * CurrentWorldRotation.Inverse();
			const FVector RotationAxis = WorldDelta.GetRotationAxis();
			const float RotationAngle = WorldDelta.GetAngle();

			if (RotationAngle > KINDA_SMALL_NUMBER)
			{
				const FVector BoneLocalAxis = State.InitialWorldTransform.InverseTransformVector(RotationAxis);
				const FQuat BoneLocalDelta = FQuat(BoneLocalAxis, RotationAngle);
				const FQuat NewOffset = State.InitialRotationOffset * BoneLocalDelta;

				EditorController->AssetController->SetRotationOffsetForRetargetPoseBone(
					State.BoneName, NewOffset, SourceOrTarget);
			}
		}
	}

	if (Location && bRootSelected)
	{
		// Only root supports translation
		const UIKRetargeter* Asset = EditorController->AssetController->GetAsset();
		float ComponentScale = 1.0f;
		if (Asset)
		{
			const bool bIsSource = SourceOrTarget == ERetargetSourceOrTarget::Source;
			ComponentScale = bIsSource ? 1.0f : Asset->TargetMeshScale;
		}
		ComponentScale = FMath::Max(ComponentScale, UE_KINDA_SMALL_NUMBER);

		// Compute the world-space delta and scale it
		const FVector WorldDelta = *Location - InitialStates[0].InitialWorldTransform.GetLocation();
		const FVector ScaledDelta = WorldDelta * (1.0f / ComponentScale);
		const FVector NewRootOffset = InitialRootTranslationOffset + ScaledDelta;

		EditorController->AssetController->GetCurrentRetargetPose(SourceOrTarget)
			.SetRootTranslationDelta(NewRootOffset);
	}

	// Scale: ignored — not applicable to retarget poses
}

// ============================================================================
// Transaction Handling
// ============================================================================

int32 FRetargetBoneTransformHandler::BeginTransaction(const FText& Description)
{
	// Use GEditor transactions, matching the retargeter's own HandleBeginTransform pattern
	if (!GEditor || !EditorController || !EditorController->AssetController)
	{
		return -1;
	}

	UIKRetargeter* Asset = EditorController->AssetController->GetAsset();
	if (Asset)
	{
		Asset->Modify();
	}

	return GEditor->BeginTransaction(TEXT(""), Description, nullptr);
}

void FRetargetBoneTransformHandler::EndTransaction()
{
	if (GEditor)
	{
		GEditor->EndTransaction();
	}
}

void FRetargetBoneTransformHandler::CancelTransaction(const int32 TransactionIndex)
{
	if (GEditor && TransactionIndex >= 0)
	{
		GEditor->CancelTransaction(TransactionIndex);
	}
}

// ============================================================================
// Visualization
// ============================================================================

UWorld* FRetargetBoneTransformHandler::GetVisualizationWorld() const
{
	const UDebugSkelMeshComponent* MeshComp = GetEditedMeshComponent();
	return MeshComp ? MeshComp->GetWorld() : nullptr;
}
