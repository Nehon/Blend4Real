#include "FActorTransformHandler.h"
#include "Blend4RealUtils.h"
#include "Editor.h"
#include "Engine/Selection.h"

bool FActorTransformHandler::HasSelection() const
{
	return GEditor && GEditor->GetSelectedActors()->Num() > 0;
}

int32 FActorTransformHandler::GetSelectionCount() const
{
	return GEditor ? GEditor->GetSelectedActors()->Num() : 0;
}

FTransform FActorTransformHandler::ComputeSelectionPivot() const
{
	return Blend4RealUtils::ComputeSelectionPivot();
}

FTransform FActorTransformHandler::GetFirstSelectedItemTransform() const
{
	if (!GEditor)
	{
		return FTransform::Identity;
	}

	if (const AActor* Actor = GEditor->GetSelectedActors()->GetTop<AActor>())
	{
		return InitialTransforms[Actor->GetUniqueID()];
	}

	return FTransform::Identity;
}

FVector FActorTransformHandler::ComputeAverageLocalAxis(EAxis::Type Axis) const
{
	if (!GEditor)
	{
		return FVector::ZeroVector;
	}

	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (SelectedActors->Num() == 0)
	{
		return FVector::ZeroVector;
	}

	// Accumulate axis vectors from each selected actor
	FVector AccumulatedAxis = FVector::ZeroVector;
	int32 Count = 0;

	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		if (const AActor* Actor = Cast<AActor>(*It))
		{
			if (const FTransform* Transform = InitialTransforms.Find(Actor->GetUniqueID()))
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

void FActorTransformHandler::CaptureInitialState()
{
	InitialTransforms.Empty();

	if (!GEditor)
	{
		return;
	}

	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			InitialTransforms.Add(Actor->GetUniqueID(), Actor->GetActorTransform());
		}
	}
}

void FActorTransformHandler::RestoreInitialState()
{
	if (!GEditor)
	{
		return;
	}

	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			if (const FTransform* Original = InitialTransforms.Find(Actor->GetUniqueID()))
			{
				Actor->SetActorTransform(*Original, false, nullptr, ETeleportType::None);
				// Notify actor that movement is complete (restored to original position)
				Actor->PostEditMove(true);
			}
		}
	}
}

void FActorTransformHandler::ApplyTransformAroundPivot(const FTransform& InitialPivot,
                                                       const FTransform& NewPivotTransform,
                                                       TOptional<EAxis::Type> LocalScaleAxis)
{
	if (!GEditor)
	{
		return;
	}

	// Calculate common deltas
	const FVector DeltaTranslation = NewPivotTransform.GetLocation() - InitialPivot.GetLocation();
	const FQuat DeltaRotation = NewPivotTransform.GetRotation() * InitialPivot.GetRotation().Inverse();
	const FVector DeltaScale = NewPivotTransform.GetScale3D() / InitialPivot.GetScale3D();
	const FVector PivotLocation = InitialPivot.GetLocation();

	// For local-axis scale, extract the actual scale factor from the encoded DeltaScale
	float LocalScaleFactor = 1.0f;
	if (LocalScaleAxis.IsSet())
	{
		// DeltaScale was encoded as: Direction * (ScaleFactor - 1) + 1
		// where Direction is the local axis in world coordinates
		// Recover the scale factor from the magnitude of the offset
		const FVector ScaleOffset = DeltaScale - FVector::OneVector;
		const float ScaleOffsetLength = ScaleOffset.Size();
		// Determine direction: if any component is positive, we're scaling up
		const bool bScalingUp = ScaleOffset.GetMax() > KINDA_SMALL_NUMBER;
		LocalScaleFactor = bScalingUp ? (1.0f + ScaleOffsetLength) : (1.0f - ScaleOffsetLength);
	}

	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			const FTransform* InitialActorTransform = InitialTransforms.Find(Actor->GetUniqueID());
			if (!InitialActorTransform)
			{
				continue;
			}

			FTransform ActorTransform;

			if (LocalScaleAxis.IsSet())
			{
				// Local-axis scale: apply scale in the actor's local coordinate system
				const FQuat ActorRotation = InitialActorTransform->GetRotation();

				// Calculate position: transform offset to local space, scale, transform back
				const FVector WorldOffset = InitialActorTransform->GetLocation() - PivotLocation;
				FVector LocalOffset = ActorRotation.UnrotateVector(WorldOffset);

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

				const FVector NewWorldOffset = ActorRotation.RotateVector(LocalOffset);
				const FVector NewLocation = PivotLocation + DeltaTranslation + NewWorldOffset;

				// Apply scale to actor's own scale in local space
				FVector NewScale = InitialActorTransform->GetScale3D();
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
				ActorTransform = FTransform(
					DeltaRotation * InitialActorTransform->GetRotation(),
					NewLocation,
					NewScale);
			}
			else
			{
				// Standard world-space transform (translation, rotation, uniform scale)
				ActorTransform = *InitialActorTransform * InitialPivot.Inverse();
				ActorTransform = ActorTransform * NewPivotTransform;
			}

			if (!ActorTransform.ContainsNaN())
			{
				Actor->SetActorTransform(ActorTransform, false, nullptr, ETeleportType::None);
				// Notify actor of movement (bFinished=false indicates movement is still in progress)
				Actor->PostEditMove(false);
			}
		}
	}
}

void FActorTransformHandler::SetDirectTransform(const FVector* Location, const FRotator* Rotation, const FVector* Scale)
{
	if (!GEditor)
	{
		return;
	}

	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			FTransform ActorTransform = Actor->GetActorTransform();

			if (Location)
			{
				ActorTransform.SetLocation(*Location);
			}
			if (Rotation)
			{
				ActorTransform.SetRotation(Rotation->Quaternion());
			}
			if (Scale)
			{
				ActorTransform.SetScale3D(*Scale);
			}

			if (!ActorTransform.ContainsNaN())
			{
				Actor->SetActorTransform(ActorTransform, false, nullptr, ETeleportType::None);
				// Notify actor of movement (bFinished=false indicates movement is still in progress)
				Actor->PostEditMove(false);
			}
		}
	}
}

int32 FActorTransformHandler::BeginTransaction(const FText& Description)
{
	if (!GEditor)
	{
		return -1;
	}

	const int32 TransactionIndex = GEditor->BeginTransaction(TEXT(""), Description, nullptr);

	// Mark all selected actors as modified
	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			Actor->Modify();
		}
	}

	return TransactionIndex;
}

void FActorTransformHandler::EndTransaction()
{
	if (GEditor)
	{
		// Notify all selected actors that movement has finished
		// This triggers construction script reruns, OnActorMoved broadcasts, etc.
		USelection* SelectedActors = GEditor->GetSelectedActors();
		for (FSelectionIterator It(*SelectedActors); It; ++It)
		{
			if (AActor* Actor = Cast<AActor>(*It))
			{
				Actor->PostEditMove(true);
			}
		}

		GEditor->EndTransaction();
	}
}

void FActorTransformHandler::CancelTransaction(int32 TransactionIndex)
{
	if (GEditor && TransactionIndex >= 0)
	{
		GEditor->CancelTransaction(TransactionIndex);
	}
}

const FTransform* FActorTransformHandler::GetInitialTransform(uint32 ActorUniqueID) const
{
	return InitialTransforms.Find(ActorUniqueID);
}
