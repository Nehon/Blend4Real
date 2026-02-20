// Copyright 2025-Present - Nehon (Rémy Bouquet). All Rights Reserved.

#include "FControlRigShapeTransformHandler.h"
#include "ControlRigGizmoActor.h"
#include "ControlRig.h"
#include "IControlRigObjectBinding.h"
#include "Editor.h"
#include "EngineUtils.h"

// ============================================================================
// Static Helpers
// ============================================================================

/**
 * Control Rig shapes are NOT in GEditor->GetSelectedActors().
 * FControlRigEditMode manages its own selection via URigHierarchy.
 * Shape actors store their selected state internally (IsSelectedInEditor()).
 * We iterate world actors and check that flag.
 */

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

bool FControlRigShapeTransformHandler::HasSelectedShapeActors()
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return false;
	}

	for (TActorIterator<AControlRigShapeActor> It(World); It; ++It)
	{
		const AControlRigShapeActor* ShapeActor = *It;
		if (ShapeActor->IsSelectedInEditor() && ShapeActor->ControlRig.IsValid())
		{
			return true;
		}
	}

	return false;
}

void FControlRigShapeTransformHandler::GetSelectedShapeActors(TArray<AControlRigShapeActor*>& OutShapeActors)
{
	OutShapeActors.Empty();

	const UWorld* World = GetEditorWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AControlRigShapeActor> It(World); It; ++It)
	{
		AControlRigShapeActor* ShapeActor = *It;
		if (ShapeActor->IsSelectedInEditor() && ShapeActor->ControlRig.IsValid())
		{
			OutShapeActors.Add(ShapeActor);
		}
	}
}

// ============================================================================
// Space Conversion Helpers
// ============================================================================

FTransform FControlRigShapeTransformHandler::GetComponentToWorld(const AControlRigShapeActor* ShapeActor)
{
	if (!ShapeActor || !ShapeActor->ControlRig.IsValid())
	{
		return FTransform::Identity;
	}

	UControlRig* Rig = ShapeActor->ControlRig.Get();
	TSharedPtr<IControlRigObjectBinding> Binding = Rig->GetObjectBinding();
	if (Binding.IsValid())
	{
		if (USceneComponent* BoundComponent = Cast<USceneComponent>(Binding->GetBoundObject()))
		{
			return BoundComponent->GetComponentToWorld();
		}
	}

	return FTransform::Identity;
}

FTransform FControlRigShapeTransformHandler::GetControlWorldTransform(const AControlRigShapeActor* ShapeActor)
{
	if (!ShapeActor || !ShapeActor->ControlRig.IsValid())
	{
		return FTransform::Identity;
	}

	UControlRig* Rig = ShapeActor->ControlRig.Get();
	const FTransform ComponentSpace = Rig->GetControlGlobalTransform(ShapeActor->ControlName);
	const FTransform CompToWorld = GetComponentToWorld(ShapeActor);

	return ComponentSpace * CompToWorld;
}

void FControlRigShapeTransformHandler::SetControlWorldTransform(
	const AControlRigShapeActor* ShapeActor,
	const FTransform& WorldTransform,
	const bool bSetupUndo)
{
	if (!ShapeActor || !ShapeActor->ControlRig.IsValid())
	{
		return;
	}

	UControlRig* Rig = ShapeActor->ControlRig.Get();
	const FTransform CompToWorld = GetComponentToWorld(ShapeActor);
	const FTransform ComponentSpace = WorldTransform.GetRelativeTransform(CompToWorld);

	FRigControlModifiedContext Context;
	Context.SetKey = EControlRigSetKey::DoNotCare;

	Rig->SetControlGlobalTransform(
		ShapeActor->ControlName,
		ComponentSpace,
		/*bNotify=*/ true,
		Context,
		bSetupUndo);
}

// ============================================================================
// Selection Queries
// ============================================================================

bool FControlRigShapeTransformHandler::HasSelection() const
{
	return HasSelectedShapeActors();
}

int32 FControlRigShapeTransformHandler::GetSelectionCount() const
{
	TArray<AControlRigShapeActor*> ShapeActors;
	GetSelectedShapeActors(ShapeActors);
	return ShapeActors.Num();
}

// ============================================================================
// Transform Data
// ============================================================================

FTransform FControlRigShapeTransformHandler::ComputeSelectionPivot() const
{
	TArray<AControlRigShapeActor*> ShapeActors;
	GetSelectedShapeActors(ShapeActors);

	if (ShapeActors.Num() == 0)
	{
		return FTransform::Identity;
	}

	if (ShapeActors.Num() == 1)
	{
		return GetControlWorldTransform(ShapeActors[0]);
	}

	// Multiple selection: average position, identity rotation
	FVector AverageLocation = FVector::ZeroVector;
	for (AControlRigShapeActor* ShapeActor : ShapeActors)
	{
		AverageLocation += GetControlWorldTransform(ShapeActor).GetLocation();
	}
	AverageLocation /= ShapeActors.Num();

	return FTransform(AverageLocation);
}

FTransform FControlRigShapeTransformHandler::GetFirstSelectedItemTransform() const
{
	if (InitialStates.Num() > 0)
	{
		return InitialStates[0].WorldTransform;
	}

	return FTransform::Identity;
}

FVector FControlRigShapeTransformHandler::ComputeAverageLocalAxis(const EAxis::Type Axis) const
{
	if (InitialStates.Num() == 0)
	{
		return FVector::ZeroVector;
	}

	FVector AccumulatedAxis = FVector::ZeroVector;

	for (const FControlState& State : InitialStates)
	{
		const FQuat Rotation = State.WorldTransform.GetRotation();
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

void FControlRigShapeTransformHandler::CaptureInitialState()
{
	InitialStates.Empty();

	TArray<AControlRigShapeActor*> ShapeActors;
	GetSelectedShapeActors(ShapeActors);

	for (AControlRigShapeActor* ShapeActor : ShapeActors)
	{
		FControlState State;
		State.ShapeActor = ShapeActor;
		State.ControlRig = ShapeActor->ControlRig;
		State.ControlName = ShapeActor->ControlName;
		State.WorldTransform = GetControlWorldTransform(ShapeActor);
		InitialStates.Add(MoveTemp(State));
	}
}

void FControlRigShapeTransformHandler::RestoreInitialState()
{
	for (const FControlState& State : InitialStates)
	{
		if (State.ShapeActor.IsValid() && State.ControlRig.IsValid())
		{
			SetControlWorldTransform(State.ShapeActor.Get(), State.WorldTransform, /*bSetupUndo=*/ false);
		}
	}
}

// ============================================================================
// Transform Application
// ============================================================================

void FControlRigShapeTransformHandler::ApplyTransformAroundPivot(
	const FTransform& InitialPivot,
	const FTransform& NewPivotTransform,
	TOptional<EAxis::Type> LocalScaleAxis)
{
	const FVector DeltaTranslation = NewPivotTransform.GetLocation() - InitialPivot.GetLocation();
	const FQuat DeltaRotation = NewPivotTransform.GetRotation() * InitialPivot.GetRotation().Inverse();
	const FVector DeltaScale = NewPivotTransform.GetScale3D() / InitialPivot.GetScale3D();
	const FVector PivotLocation = InitialPivot.GetLocation();

	// For local-axis scale, extract the actual scale factor
	float LocalScaleFactor = 1.0f;
	if (LocalScaleAxis.IsSet())
	{
		const FVector ScaleOffset = DeltaScale - FVector::OneVector;
		const float ScaleOffsetLength = ScaleOffset.Size();
		const bool bScalingUp = ScaleOffset.GetMax() > KINDA_SMALL_NUMBER;
		LocalScaleFactor = bScalingUp ? (1.0f + ScaleOffsetLength) : (1.0f - ScaleOffsetLength);
	}

	for (const FControlState& State : InitialStates)
	{
		if (!State.ShapeActor.IsValid() || !State.ControlRig.IsValid())
		{
			continue;
		}

		FTransform NewWorldTransform;

		if (LocalScaleAxis.IsSet())
		{
			const FQuat ActorRotation = State.WorldTransform.GetRotation();

			const FVector WorldOffset = State.WorldTransform.GetLocation() - PivotLocation;
			FVector LocalOffset = ActorRotation.UnrotateVector(WorldOffset);

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

			const FVector NewWorldOffset = ActorRotation.RotateVector(LocalOffset);
			const FVector NewLocation = PivotLocation + DeltaTranslation + NewWorldOffset;

			FVector NewScale = State.WorldTransform.GetScale3D();
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

			NewWorldTransform = FTransform(
				DeltaRotation * State.WorldTransform.GetRotation(),
				NewLocation,
				NewScale);
		}
		else
		{
			NewWorldTransform = State.WorldTransform * InitialPivot.Inverse();
			NewWorldTransform = NewWorldTransform * NewPivotTransform;
		}

		if (!NewWorldTransform.ContainsNaN())
		{
			SetControlWorldTransform(State.ShapeActor.Get(), NewWorldTransform, /*bSetupUndo=*/ true);
		}
	}
}

void FControlRigShapeTransformHandler::SetDirectTransform(
	const FVector* Location,
	const FRotator* Rotation,
	const FVector* Scale)
{
	TArray<AControlRigShapeActor*> ShapeActors;
	GetSelectedShapeActors(ShapeActors);

	for (AControlRigShapeActor* ShapeActor : ShapeActors)
	{
		FTransform CurrentTransform = GetControlWorldTransform(ShapeActor);

		if (Location)
		{
			CurrentTransform.SetLocation(*Location);
		}
		if (Rotation)
		{
			CurrentTransform.SetRotation(Rotation->Quaternion());
		}
		if (Scale)
		{
			CurrentTransform.SetScale3D(*Scale);
		}

		if (!CurrentTransform.ContainsNaN())
		{
			SetControlWorldTransform(ShapeActor, CurrentTransform, /*bSetupUndo=*/ true);
		}
	}
}

// ============================================================================
// Transaction Handling
// ============================================================================

int32 FControlRigShapeTransformHandler::BeginTransaction(const FText& Description)
{
	if (!GEditor)
	{
		return -1;
	}

	const int32 TransactionIndex = GEditor->BeginTransaction(TEXT(""), Description, nullptr);

	// Mark all affected rigs as modified for undo
	TSet<UControlRig*> ModifiedRigs;
	for (const FControlState& State : InitialStates)
	{
		if (State.ControlRig.IsValid())
		{
			UControlRig* Rig = State.ControlRig.Get();
			if (!ModifiedRigs.Contains(Rig))
			{
				Rig->Modify();
				ModifiedRigs.Add(Rig);
			}
		}
	}

	return TransactionIndex;
}

void FControlRigShapeTransformHandler::EndTransaction()
{
	if (GEditor)
	{
		GEditor->EndTransaction();
	}
}

void FControlRigShapeTransformHandler::CancelTransaction(const int32 TransactionIndex)
{
	if (GEditor && TransactionIndex >= 0)
	{
		GEditor->CancelTransaction(TransactionIndex);
	}
}
