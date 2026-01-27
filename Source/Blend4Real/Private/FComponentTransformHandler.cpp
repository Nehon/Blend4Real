#include "FComponentTransformHandler.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Components/SceneComponent.h"

USelection* FComponentTransformHandler::GetSelectedComponents() const
{
	return GEditor ? GEditor->GetSelectedComponents() : nullptr;
}

bool FComponentTransformHandler::HasSelection() const
{
	USelection* Selection = GetSelectedComponents();
	return Selection && Selection->Num() > 0;
}

int32 FComponentTransformHandler::GetSelectionCount() const
{
	USelection* Selection = GetSelectedComponents();
	return Selection ? Selection->Num() : 0;
}

FTransform FComponentTransformHandler::ComputeSelectionPivot() const
{
	return Blend4RealUtils::ComputeSelectionPivot();
}

FTransform FComponentTransformHandler::GetFirstSelectedItemTransform() const
{
	USelection* Selection = GetSelectedComponents();
	if (!Selection)
	{
		return FTransform::Identity;
	}

	if (USceneComponent* Component = Selection->GetTop<USceneComponent>())
	{
		return InitialTransforms[Component->GetUniqueID()];
	}

	return FTransform::Identity;
}

FVector FComponentTransformHandler::ComputeAverageLocalAxis(EAxis::Type Axis) const
{
	USelection* Selection = GetSelectedComponents();
	if (!Selection || Selection->Num() == 0)
	{
		return FVector::ZeroVector;
	}

	// Accumulate axis vectors from each selected component
	FVector AccumulatedAxis = FVector::ZeroVector;
	int32 Count = 0;

	for (FSelectionIterator It(*Selection); It; ++It)
	{
		if (USceneComponent* Component = Cast<USceneComponent>(*It))
		{
			if (const FTransform* Transform = InitialTransforms.Find(Component->GetUniqueID()))
			{
				const FQuat Rotation = Transform->GetRotation();
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
				Count++;
			}
		}
	}

	if (Count == 0)
	{
		return FVector::ZeroVector;
	}

	return (AccumulatedAxis / Count).GetSafeNormal();
}

void FComponentTransformHandler::CaptureInitialState()
{
	InitialTransforms.Empty();

	USelection* Selection = GetSelectedComponents();
	if (!Selection)
	{
		return;
	}

	for (FSelectionIterator It(*Selection); It; ++It)
	{
		if (USceneComponent* Component = Cast<USceneComponent>(*It))
		{
			// Store world transform for computing deltas later
			InitialTransforms.Add(Component->GetUniqueID(), Component->GetComponentTransform());
		}
	}
}

void FComponentTransformHandler::RestoreInitialState()
{
	USelection* Selection = GetSelectedComponents();
	if (!Selection)
	{
		return;
	}

	for (FSelectionIterator It(*Selection); It; ++It)
	{
		if (USceneComponent* Component = Cast<USceneComponent>(*It))
		{
			if (const FTransform* Original = InitialTransforms.Find(Component->GetUniqueID()))
			{
				Component->SetWorldTransform(*Original);
				// Notify component that movement is complete (restored to original position)
				Component->PostEditComponentMove(true);
			}
		}
	}
}

void FComponentTransformHandler::ApplyTransformAroundPivot(const FTransform& InitialPivot,
                                                           const FTransform& NewPivotTransform,
                                                           TOptional<EAxis::Type> LocalScaleAxis)
{
	USelection* Selection = GetSelectedComponents();
	if (!Selection)
	{
		return;
	}

	// Calculate the delta between initial and new pivot transforms
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

	for (FSelectionIterator It(*Selection); It; ++It)
	{
		if (USceneComponent* Component = Cast<USceneComponent>(*It))
		{
			const FTransform* InitialComponentTransform = InitialTransforms.Find(Component->GetUniqueID());
			if (!InitialComponentTransform)
			{
				continue;
			}

			FVector NewLocation;
			FQuat NewRotation;
			FVector NewScale;

			if (LocalScaleAxis.IsSet())
			{
				// Local-axis scale: apply scale in the component's local coordinate system
				const FQuat ComponentRotation = InitialComponentTransform->GetRotation();

				// Calculate position: transform offset to local space, scale, transform back
				const FVector WorldOffset = InitialComponentTransform->GetLocation() - PivotLocation;
				FVector LocalOffset = ComponentRotation.UnrotateVector(WorldOffset);

				// Scale only the specified axis in local space
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

				const FVector NewWorldOffset = ComponentRotation.RotateVector(LocalOffset);
				NewLocation = PivotLocation + DeltaTranslation + NewWorldOffset;

				// Apply scale to component's own scale in local space
				NewScale = InitialComponentTransform->GetScale3D();
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

				// Rotation is unchanged for scale operations
				NewRotation = DeltaRotation * InitialComponentTransform->GetRotation();
			}
			else
			{
				// Standard world-space transform
				const FVector InitialRelativeToPivot = InitialComponentTransform->GetLocation() - PivotLocation;
				const FVector RotatedOffset = DeltaRotation.RotateVector(InitialRelativeToPivot);
				const FVector ScaledOffset = RotatedOffset * DeltaScale;

				NewLocation = PivotLocation + DeltaTranslation + ScaledOffset;
				NewRotation = DeltaRotation * InitialComponentTransform->GetRotation();
				NewScale = InitialComponentTransform->GetScale3D() * DeltaScale;
			}

			// Build new transform
			FTransform NewTransform(NewRotation, NewLocation, NewScale);

			if (NewTransform.IsValid())
			{
				Component->SetWorldTransform(NewTransform);
				// Notify component of movement (bFinished=false indicates movement is still in progress)
				Component->PostEditComponentMove(false);
			}
		}
	}
}

void FComponentTransformHandler::SetDirectTransform(const FVector* Location, const FRotator* Rotation,
                                                    const FVector* Scale)
{
	USelection* Selection = GetSelectedComponents();
	if (!Selection)
	{
		return;
	}

	for (FSelectionIterator It(*Selection); It; ++It)
	{
		if (USceneComponent* Component = Cast<USceneComponent>(*It))
		{
			FTransform CurrentTransform = Component->GetComponentTransform();

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

			Component->SetWorldTransform(CurrentTransform);
				// Notify component of movement (bFinished=false indicates movement is still in progress)
				Component->PostEditComponentMove(false);
		}
	}
}

int32 FComponentTransformHandler::BeginTransaction(const FText& Description)
{
	if (!GEditor)
	{
		return -1;
	}

	int32 TransactionIndex = GEditor->BeginTransaction(TEXT(""), Description, nullptr);

	// Mark all selected components as modified
	USelection* Selection = GetSelectedComponents();
	if (Selection)
	{
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			if (USceneComponent* Component = Cast<USceneComponent>(*It))
			{
				Component->Modify();
			}
		}
	}

	return TransactionIndex;
}

void FComponentTransformHandler::EndTransaction()
{
	if (GEditor)
	{
		// Notify all selected components that movement has finished
		USelection* Selection = GetSelectedComponents();
		if (Selection)
		{
			for (FSelectionIterator It(*Selection); It; ++It)
			{
				if (USceneComponent* Component = Cast<USceneComponent>(*It))
				{
					Component->PostEditComponentMove(true);
				}
			}
		}

		GEditor->EndTransaction();
	}
}

void FComponentTransformHandler::CancelTransaction(int32 TransactionIndex)
{
	if (GEditor && TransactionIndex >= 0)
	{
		GEditor->CancelTransaction(TransactionIndex);
	}
}
