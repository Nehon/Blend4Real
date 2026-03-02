// Copyright 2025-Present - Nehon (Rémy Bouquet). All Rights Reserved.

#include "FEditSkeletonTransformHandler.h"
#include "SkeletalMesh/SkeletonEditingTool.h"

// ============================================================================
// Construction
// ============================================================================

FEditSkeletonTransformHandler::FEditSkeletonTransformHandler(
	USkeletonEditingTool* InTool,
	UWorld* InPreviewWorld)
	: SkeletonTool(InTool)
	, PreviewWorld(InPreviewWorld)
{
}

// ============================================================================
// Selection Queries
// ============================================================================

bool FEditSkeletonTransformHandler::HasSelection() const
{
	return SkeletonTool && SkeletonTool->GetSelection().Num() > 0;
}

int32 FEditSkeletonTransformHandler::GetSelectionCount() const
{
	return SkeletonTool ? SkeletonTool->GetSelection().Num() : 0;
}

// ============================================================================
// Transform Data
// ============================================================================

FTransform FEditSkeletonTransformHandler::ComputeSelectionPivot() const
{
	if (!SkeletonTool)
	{
		return FTransform::Identity;
	}

	const TArray<FName>& Selection = SkeletonTool->GetSelection();
	if (Selection.Num() == 0)
	{
		return FTransform::Identity;
	}

	// Match the tool's own gizmo behavior: pivot at the last selected bone
	return SkeletonTool->GetTransform(Selection.Last(), /*bWorld=*/true);
}

FTransform FEditSkeletonTransformHandler::GetFirstSelectedItemTransform() const
{
	if (InitialStates.Num() > 0)
	{
		return InitialStates[0].InitialWorldTransform;
	}

	return ComputeSelectionPivot();
}

FVector FEditSkeletonTransformHandler::ComputeAverageLocalAxis(const EAxis::Type Axis) const
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

void FEditSkeletonTransformHandler::CaptureInitialState()
{
	InitialStates.Empty();

	if (!SkeletonTool)
	{
		return;
	}

	const TArray<FName>& Selection = SkeletonTool->GetSelection();
	for (const FName& BoneName : Selection)
	{
		FBoneState State;
		State.BoneName = BoneName;
		State.InitialWorldTransform = SkeletonTool->GetTransform(BoneName, /*bWorld=*/true);
		State.InitialLocalTransform = SkeletonTool->GetTransform(BoneName, /*bWorld=*/false);
		InitialStates.Add(MoveTemp(State));
	}
}

void FEditSkeletonTransformHandler::RestoreInitialState()
{
	if (!SkeletonTool || InitialStates.Num() == 0)
	{
		return;
	}

	TArray<FName> BoneNames;
	TArray<FTransform> LocalTransforms;
	BoneNames.Reserve(InitialStates.Num());
	LocalTransforms.Reserve(InitialStates.Num());

	for (const FBoneState& State : InitialStates)
	{
		BoneNames.Add(State.BoneName);
		LocalTransforms.Add(State.InitialLocalTransform);
	}

	SkeletonTool->SetTransforms(BoneNames, LocalTransforms, /*bWorld=*/false);
}

// ============================================================================
// Transform Application
// ============================================================================

void FEditSkeletonTransformHandler::ApplyTransformAroundPivot(
	const FTransform& InitialPivot,
	const FTransform& NewPivotTransform,
	TOptional<EAxis::Type> LocalScaleAxis)
{
	if (!SkeletonTool || InitialStates.Num() == 0)
	{
		return;
	}

	const FVector DeltaTranslation = NewPivotTransform.GetLocation() - InitialPivot.GetLocation();
	const FQuat DeltaRotation = NewPivotTransform.GetRotation() * InitialPivot.GetRotation().Inverse();
	const FVector DeltaScale = NewPivotTransform.GetScale3D() / InitialPivot.GetScale3D();
	const FVector PivotLocation = InitialPivot.GetLocation();

	// For local-axis scale, extract the actual scale factor from the encoded DeltaScale
	float LocalScaleFactor = 1.0f;
	if (LocalScaleAxis.IsSet())
	{
		const FVector ScaleOffset = DeltaScale - FVector::OneVector;
		const float ScaleOffsetLength = ScaleOffset.Size();
		const bool bScalingUp = ScaleOffset.GetMax() > KINDA_SMALL_NUMBER;
		LocalScaleFactor = bScalingUp ? (1.0f + ScaleOffsetLength) : (1.0f - ScaleOffsetLength);
	}

	TArray<FName> BoneNames;
	TArray<FTransform> NewWorldTransforms;
	BoneNames.Reserve(InitialStates.Num());
	NewWorldTransforms.Reserve(InitialStates.Num());

	for (const FBoneState& State : InitialStates)
	{
		FTransform NewTransform;

		if (LocalScaleAxis.IsSet())
		{
			const FQuat BoneRotation = State.InitialWorldTransform.GetRotation();

			// Calculate position: transform offset to local space, scale the axis, transform back
			const FVector WorldOffset = State.InitialWorldTransform.GetLocation() - PivotLocation;
			FVector LocalOffset = BoneRotation.UnrotateVector(WorldOffset);

			switch (LocalScaleAxis.GetValue())
			{
			case EAxis::X:
				LocalOffset.X *= LocalScaleFactor;
				break;
			case EAxis::Y:
				LocalOffset.Y *= LocalScaleFactor;
				break;
			case EAxis::Z:
				LocalOffset.Z *= LocalScaleFactor;
				break;
			default:
				break;
			}

			const FVector NewWorldOffset = BoneRotation.RotateVector(LocalOffset);
			const FVector NewLocation = PivotLocation + DeltaTranslation + NewWorldOffset;

			// Apply scale to bone's own scale in local space
			FVector NewScale = State.InitialWorldTransform.GetScale3D();
			switch (LocalScaleAxis.GetValue())
			{
			case EAxis::X:
				NewScale.X *= LocalScaleFactor;
				break;
			case EAxis::Y:
				NewScale.Y *= LocalScaleFactor;
				break;
			case EAxis::Z:
				NewScale.Z *= LocalScaleFactor;
				break;
			default:
				break;
			}

			NewTransform = FTransform(
				DeltaRotation * BoneRotation,
				NewLocation,
				NewScale);
		}
		else
		{
			// Standard world-space transform (translation, rotation, uniform scale)
			const FVector WorldOffset = State.InitialWorldTransform.GetLocation() - PivotLocation;
			const FVector RotatedOffset = DeltaRotation.RotateVector(WorldOffset);
			const FVector ScaledOffset = RotatedOffset * DeltaScale;
			const FVector NewLocation = PivotLocation + DeltaTranslation + ScaledOffset;
			const FQuat NewRotation = DeltaRotation * State.InitialWorldTransform.GetRotation();
			const FVector NewScale = State.InitialWorldTransform.GetScale3D() * DeltaScale;

			NewTransform = FTransform(NewRotation, NewLocation, NewScale);
		}

		if (!NewTransform.ContainsNaN())
		{
			BoneNames.Add(State.BoneName);
			NewWorldTransforms.Add(NewTransform);
		}
	}

	if (BoneNames.Num() > 0)
	{
		SkeletonTool->SetTransforms(BoneNames, NewWorldTransforms, /*bWorld=*/true);
	}
}

void FEditSkeletonTransformHandler::SetDirectTransform(
	const FVector* Location,
	const FRotator* Rotation,
	const FVector* Scale)
{
	if (!SkeletonTool || InitialStates.Num() == 0)
	{
		return;
	}

	TArray<FName> BoneNames;
	TArray<FTransform> NewWorldTransforms;
	BoneNames.Reserve(InitialStates.Num());
	NewWorldTransforms.Reserve(InitialStates.Num());

	for (const FBoneState& State : InitialStates)
	{
		FTransform NewTransform = State.InitialWorldTransform;

		if (Location)
		{
			NewTransform.SetLocation(*Location);
		}
		if (Rotation)
		{
			NewTransform.SetRotation(Rotation->Quaternion());
		}
		if (Scale)
		{
			NewTransform.SetScale3D(*Scale);
		}

		BoneNames.Add(State.BoneName);
		NewWorldTransforms.Add(NewTransform);
	}

	SkeletonTool->SetTransforms(BoneNames, NewWorldTransforms, /*bWorld=*/true);
}

// ============================================================================
// Transaction Handling
// ============================================================================

int32 FEditSkeletonTransformHandler::BeginTransaction(const FText& Description)
{
	// The tool's undo system (BeginChange/EndChange) is protected and not accessible.
	// Cancel during drag works via RestoreInitialState.
	return -1;
}

void FEditSkeletonTransformHandler::EndTransaction()
{
	// No-op: see BeginTransaction comment
}

void FEditSkeletonTransformHandler::CancelTransaction(const int32 TransactionIndex)
{
	// No-op: cancel is handled by RestoreInitialState
}

// ============================================================================
// Visualization
// ============================================================================

UWorld* FEditSkeletonTransformHandler::GetVisualizationWorld() const
{
	return PreviewWorld.Get();
}
