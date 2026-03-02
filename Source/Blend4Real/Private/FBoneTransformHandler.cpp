// Copyright 2025-Present - Nehon (Rémy Bouquet). All Rights Reserved.

#include "FBoneTransformHandler.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "AnimPreviewInstance.h"
#include "BoneControllers/AnimNode_ModifyBone.h"
#include "Editor.h"

// ============================================================================
// Construction
// ============================================================================

FBoneTransformHandler::FBoneTransformHandler(IPersonaPreviewScene* InPreviewScene)
	: PreviewScene(InPreviewScene)
{
	if (PreviewScene)
	{
		PreviewWorld = PreviewScene->GetWorld();
	}
}

// ============================================================================
// Helpers
// ============================================================================

bool FBoneTransformHandler::GetPreviewComponents(
	UDebugSkelMeshComponent*& OutMeshComp,
	UAnimPreviewInstance*& OutPreviewInstance) const
{
	if (!PreviewScene || !PreviewWorld.IsValid())
	{
		return false;
	}

	OutMeshComp = PreviewScene->GetPreviewMeshComponent();
	if (!OutMeshComp)
	{
		return false;
	}

	OutPreviewInstance = Cast<UAnimPreviewInstance>(OutMeshComp->GetAnimInstance());
	return OutPreviewInstance != nullptr;
}

// ============================================================================
// Selection Queries
// ============================================================================

bool FBoneTransformHandler::HasSelection() const
{
	return PreviewScene->GetSelectedBoneIndex() != INDEX_NONE;
}

int32 FBoneTransformHandler::GetSelectionCount() const
{
	return HasSelection() ? 1 : 0;
}

// ============================================================================
// Transform Data
// ============================================================================

FTransform FBoneTransformHandler::ComputeSelectionPivot() const
{
	UDebugSkelMeshComponent* MeshComp = nullptr;
	UAnimPreviewInstance* PreviewInstance = nullptr;
	if (!GetPreviewComponents(MeshComp, PreviewInstance))
	{
		return FTransform::Identity;
	}

	const int32 BoneIndex = PreviewScene->GetSelectedBoneIndex();
	if (BoneIndex == INDEX_NONE)
	{
		return FTransform::Identity;
	}

	return MeshComp->GetBoneTransform(BoneIndex);
}

FTransform FBoneTransformHandler::GetFirstSelectedItemTransform() const
{
	if (InitialStates.Num() > 0)
	{
		return InitialStates[0].InitialWorldTransform;
	}

	return ComputeSelectionPivot();
}

FVector FBoneTransformHandler::ComputeAverageLocalAxis(const EAxis::Type Axis) const
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

void FBoneTransformHandler::CaptureInitialState()
{
	InitialStates.Empty();

	UDebugSkelMeshComponent* MeshComp = nullptr;
	UAnimPreviewInstance* PreviewInstance = nullptr;
	if (!GetPreviewComponents(MeshComp, PreviewInstance))
	{
		return;
	}

	const int32 BoneIndex = PreviewScene->GetSelectedBoneIndex();
	if (BoneIndex == INDEX_NONE)
	{
		return;
	}

	const FName BoneName = MeshComp->GetSkeletalMeshAsset()
		? MeshComp->GetSkeletalMeshAsset()->GetRefSkeleton().GetBoneName(BoneIndex)
		: NAME_None;
	if (BoneName == NAME_None)
	{
		return;
	}

	// Get or create bone modifier
	FAnimNode_ModifyBone& SkelControl = PreviewInstance->ModifyBone(BoneName);

	FBoneState State;
	State.BoneIndex = BoneIndex;
	State.BoneName = BoneName;
	State.InitialWorldTransform = MeshComp->GetBoneTransform(BoneIndex);
	State.InitialSkelControlTranslation = SkelControl.Translation;
	State.InitialSkelControlRotation = SkelControl.Rotation;
	State.InitialSkelControlScale = SkelControl.Scale;

	// Compute BaseTM: converts world-space vectors to bone-modification space
	// BaseTM = SkelControlTM * WorldTM^-1 (via GetRelativeTransformReverse)
	const FTransform CurrentSkelControlTM(
		SkelControl.Rotation.Quaternion(),
		SkelControl.Translation,
		SkelControl.Scale);
	State.BaseTM = State.InitialWorldTransform.GetRelativeTransformReverse(CurrentSkelControlTM);

	InitialStates.Add(MoveTemp(State));
}

void FBoneTransformHandler::RestoreInitialState()
{
	UDebugSkelMeshComponent* MeshComp = nullptr;
	UAnimPreviewInstance* PreviewInstance = nullptr;
	if (!GetPreviewComponents(MeshComp, PreviewInstance))
	{
		return;
	}

	for (const FBoneState& State : InitialStates)
	{
		FAnimNode_ModifyBone* SkelControl = PreviewInstance->FindModifiedBone(State.BoneName);
		if (SkelControl)
		{
			SkelControl->Translation = State.InitialSkelControlTranslation;
			SkelControl->Rotation = State.InitialSkelControlRotation;
			SkelControl->Scale = State.InitialSkelControlScale;
		}
	}
}

// ============================================================================
// Transform Application
// ============================================================================

void FBoneTransformHandler::ApplyTransformAroundPivot(
	const FTransform& InitialPivot,
	const FTransform& NewPivotTransform,
	TOptional<EAxis::Type> LocalScaleAxis)
{
	UDebugSkelMeshComponent* MeshComp = nullptr;
	UAnimPreviewInstance* PreviewInstance = nullptr;
	if (!GetPreviewComponents(MeshComp, PreviewInstance))
	{
		return;
	}

	const FVector DeltaTranslation = NewPivotTransform.GetLocation() - InitialPivot.GetLocation();
	const FQuat DeltaRotation = NewPivotTransform.GetRotation() * InitialPivot.GetRotation().Inverse();
	const FVector PivotLocation = InitialPivot.GetLocation();

	for (const FBoneState& State : InitialStates)
	{
		FAnimNode_ModifyBone& SkelControl = PreviewInstance->ModifyBone(State.BoneName);

		// Compute desired new world position (same math as other handlers)
		const FVector WorldOffset = State.InitialWorldTransform.GetLocation() - PivotLocation;
		const FVector RotatedOffset = DeltaRotation.RotateVector(WorldOffset);
		const FVector NewWorldPos = PivotLocation + DeltaTranslation + RotatedOffset;

		// Compute desired new world rotation
		const FQuat NewWorldRot = DeltaRotation * State.InitialWorldTransform.GetRotation();

		// --- Translation: convert world delta to bone-modification space ---
		const FVector WorldPosDelta = NewWorldPos - State.InitialWorldTransform.GetLocation();
		const FVector BoneSpaceDelta = State.BaseTM.TransformVector(WorldPosDelta);
		SkelControl.Translation = State.InitialSkelControlTranslation + BoneSpaceDelta;

		// --- Rotation: convert world rotation to bone-modification space ---
		// BaseWorldRot = the bone's world rotation WITHOUT SkelControl modification
		const FQuat BaseWorldRot = State.InitialWorldTransform.GetRotation()
			* FQuat(State.InitialSkelControlRotation).Inverse();
		// DesiredSkelControlRot = BaseWorldRot^-1 * DesiredWorldRot
		const FQuat NewSkelControlRot = BaseWorldRot.Inverse() * NewWorldRot;
		SkelControl.Rotation = NewSkelControlRot.Rotator();
	}
}

void FBoneTransformHandler::SetDirectTransform(
	const FVector* Location,
	const FRotator* Rotation,
	const FVector* Scale)
{
	UDebugSkelMeshComponent* MeshComp = nullptr;
	UAnimPreviewInstance* PreviewInstance = nullptr;
	if (!GetPreviewComponents(MeshComp, PreviewInstance))
	{
		return;
	}

	for (const FBoneState& State : InitialStates)
	{
		FAnimNode_ModifyBone& SkelControl = PreviewInstance->ModifyBone(State.BoneName);

		if (Location)
		{
			// Convert world location to bone-space translation offset
			const FVector WorldDelta = *Location - State.InitialWorldTransform.GetLocation();
			SkelControl.Translation = State.InitialSkelControlTranslation
				+ State.BaseTM.TransformVector(WorldDelta);
		}
		if (Rotation)
		{
			const FQuat BaseWorldRot = State.InitialWorldTransform.GetRotation()
				* FQuat(State.InitialSkelControlRotation).Inverse();
			SkelControl.Rotation = (BaseWorldRot.Inverse() * Rotation->Quaternion()).Rotator();
		}
		if (Scale)
		{
			SkelControl.Scale = *Scale;
		}
	}
}

// ============================================================================
// Transaction Handling
// ============================================================================

int32 FBoneTransformHandler::BeginTransaction(const FText& Description)
{
	if (!GEditor)
	{
		return -1;
	}

	UDebugSkelMeshComponent* MeshComp = nullptr;
	UAnimPreviewInstance* PreviewInstance = nullptr;
	if (GetPreviewComponents(MeshComp, PreviewInstance))
	{
		PreviewInstance->SetFlags(RF_Transactional);
		PreviewInstance->Modify();
	}

	return GEditor->BeginTransaction(TEXT(""), Description, nullptr);
}

void FBoneTransformHandler::EndTransaction()
{
	if (GEditor)
	{
		GEditor->EndTransaction();
	}
}

void FBoneTransformHandler::CancelTransaction(const int32 TransactionIndex)
{
	if (GEditor && TransactionIndex >= 0)
	{
		GEditor->CancelTransaction(TransactionIndex);
	}
}

// ============================================================================
// Visualization
// ============================================================================

UWorld* FBoneTransformHandler::GetVisualizationWorld() const
{
	return PreviewWorld.Get();
}
