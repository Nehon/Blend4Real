// Copyright 2025-Present - Nehon (Rémy Bouquet). All Rights Reserved.

#include "FControlRigPreviewTransformHandler.h"
#include "ControlRigGizmoActor.h"
#include "ControlRig.h"
#include "IControlRigObjectBinding.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Rigs/RigHierarchyDefines.h"

// ============================================================================
// Construction
// ============================================================================

FControlRigPreviewTransformHandler::FControlRigPreviewTransformHandler(UWorld* InPreviewWorld)
	: PreviewWorld(InPreviewWorld)
{
}

// ============================================================================
// Static Helpers
// ============================================================================

bool FControlRigPreviewTransformHandler::HasSelectedShapeActors(UWorld* World)
{
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

void FControlRigPreviewTransformHandler::GetSelectedShapeActors(UWorld* World, TArray<AControlRigShapeActor*>& OutShapeActors)
{
	OutShapeActors.Empty();

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

FTransform FControlRigPreviewTransformHandler::GetComponentToWorld(const AControlRigShapeActor* ShapeActor)
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

FTransform FControlRigPreviewTransformHandler::GetControlWorldTransform(const AControlRigShapeActor* ShapeActor)
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

void FControlRigPreviewTransformHandler::SetControlWorldTransform(
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
	Context.SetKey = EControlRigSetKey::Never;

	Rig->SetControlGlobalTransform(
		ShapeActor->ControlName,
		ComponentSpace,
		/*bNotify=*/ true,
		Context,
		bSetupUndo,
		/*bPrintPythonCommands=*/ false,
		/*bFixEulerFlips=*/ true);
}

// ============================================================================
// Selection Queries
// ============================================================================

bool FControlRigPreviewTransformHandler::HasSelection() const
{
	return HasSelectedShapeActors(PreviewWorld.Get());
}

int32 FControlRigPreviewTransformHandler::GetSelectionCount() const
{
	TArray<AControlRigShapeActor*> ShapeActors;
	GetSelectedShapeActors(PreviewWorld.Get(), ShapeActors);
	return ShapeActors.Num();
}

// ============================================================================
// Transform Data
// ============================================================================

FTransform FControlRigPreviewTransformHandler::ComputeSelectionPivot() const
{
	TArray<AControlRigShapeActor*> ShapeActors;
	GetSelectedShapeActors(PreviewWorld.Get(), ShapeActors);

	if (ShapeActors.Num() == 0)
	{
		return FTransform::Identity;
	}

	if (ShapeActors.Num() == 1)
	{
		return GetControlWorldTransform(ShapeActors[0]);
	}

	FVector AverageLocation = FVector::ZeroVector;
	for (AControlRigShapeActor* ShapeActor : ShapeActors)
	{
		AverageLocation += GetControlWorldTransform(ShapeActor).GetLocation();
	}
	AverageLocation /= ShapeActors.Num();

	return FTransform(AverageLocation);
}

FTransform FControlRigPreviewTransformHandler::GetFirstSelectedItemTransform() const
{
	if (InitialStates.Num() > 0)
	{
		return InitialStates[0].WorldTransform;
	}

	return FTransform::Identity;
}

FVector FControlRigPreviewTransformHandler::ComputeAverageLocalAxis(const EAxis::Type Axis) const
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

void FControlRigPreviewTransformHandler::CaptureInitialState()
{
	InitialStates.Empty();

	TArray<AControlRigShapeActor*> ShapeActors;
	GetSelectedShapeActors(PreviewWorld.Get(), ShapeActors);

	TMap<UControlRig*, TArray<FRigElementKey>> RigToKeys;

	for (AControlRigShapeActor* ShapeActor : ShapeActors)
	{
		FControlState State;
		State.ShapeActor = ShapeActor;
		State.ControlRig = ShapeActor->ControlRig;
		State.ControlName = ShapeActor->ControlName;
		State.WorldTransform = GetControlWorldTransform(ShapeActor);
		InitialStates.Add(MoveTemp(State));

		if (ShapeActor->ControlRig.IsValid())
		{
			RigToKeys.FindOrAdd(ShapeActor->ControlRig.Get()).Add(
				FRigElementKey(ShapeActor->ControlName, ERigElementType::Control));
		}
	}

	InteractionScopes.Empty();
	for (auto& [Rig, Keys] : RigToKeys)
	{
		Rig->Modify();
		InteractionScopes.Add(MakeShared<FControlRigInteractionScope>(Rig, MoveTemp(Keys)));
	}
}

void FControlRigPreviewTransformHandler::RestoreInitialState()
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

void FControlRigPreviewTransformHandler::ApplyTransformAroundPivot(
	const FTransform& InitialPivot,
	const FTransform& NewPivotTransform,
	TOptional<EAxis::Type> LocalScaleAxis)
{
	const FVector DeltaTranslation = NewPivotTransform.GetLocation() - InitialPivot.GetLocation();
	const FQuat DeltaRotation = NewPivotTransform.GetRotation() * InitialPivot.GetRotation().Inverse();
	const FVector DeltaScale = NewPivotTransform.GetScale3D() / InitialPivot.GetScale3D();
	const FVector PivotLocation = InitialPivot.GetLocation();

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

void FControlRigPreviewTransformHandler::SetDirectTransform(
	const FVector* Location,
	const FRotator* Rotation,
	const FVector* Scale)
{
	TArray<AControlRigShapeActor*> ShapeActors;
	GetSelectedShapeActors(PreviewWorld.Get(), ShapeActors);

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

int32 FControlRigPreviewTransformHandler::BeginTransaction(const FText& Description)
{
	if (!GEditor)
	{
		return -1;
	}

	return GEditor->BeginTransaction(TEXT(""), Description, nullptr);
}

void FControlRigPreviewTransformHandler::EndTransaction()
{
	// No ControlModified broadcast — the CR Editor preview scene has no sequencer,
	// so there are no track channel defaults to update.

	if (GEditor)
	{
		GEditor->EndTransaction();
	}

	InteractionScopes.Empty();
}

void FControlRigPreviewTransformHandler::CancelTransaction(const int32 TransactionIndex)
{
	if (GEditor && TransactionIndex >= 0)
	{
		GEditor->CancelTransaction(TransactionIndex);
	}

	InteractionScopes.Empty();
}

// ============================================================================
// Visualization
// ============================================================================

UWorld* FControlRigPreviewTransformHandler::GetVisualizationWorld() const
{
	return PreviewWorld.Get();
}
