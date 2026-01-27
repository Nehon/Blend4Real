#include "FSplinePointTransformHandler.h"
#include "Components/SplineComponent.h"
#include "Editor.h"

FSplinePointTransformHandler::FSplinePointTransformHandler(USplineComponent* InSplineComp, const TSet<int32>& InSelectedKeys)
	: SplineComponent(InSplineComp)
	, SelectedPointIndices(InSelectedKeys)
{
}

bool FSplinePointTransformHandler::HasSelection() const
{
	return SplineComponent.IsValid() && SelectedPointIndices.Num() > 0;
}

int32 FSplinePointTransformHandler::GetSelectionCount() const
{
	return SelectedPointIndices.Num();
}

FTransform FSplinePointTransformHandler::ComputeSelectionPivot() const
{
	if (!SplineComponent.IsValid() || SelectedPointIndices.Num() == 0)
	{
		return FTransform::Identity;
	}

	FVector Pivot = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;

	for (int32 Index : SelectedPointIndices)
	{
		Pivot += SplineComponent->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World);
	}
	Pivot /= SelectedPointIndices.Num();

	// Use the rotation of the first selected point for single selection
	if (SelectedPointIndices.Num() == 1)
	{
		const int32 FirstIndex = *SelectedPointIndices.CreateConstIterator();
		Rotation = SplineComponent->GetQuaternionAtSplinePoint(FirstIndex, ESplineCoordinateSpace::World);
	}

	return FTransform(Rotation, Pivot, FVector::OneVector);
}

FTransform FSplinePointTransformHandler::GetFirstSelectedItemTransform() const
{
	if (!SplineComponent.IsValid() || SelectedPointIndices.Num() == 0)
	{
		return FTransform::Identity;
	}

	const int32 FirstIndex = *SelectedPointIndices.CreateConstIterator();
	const FPointState* State = InitialPointStates.Find(FirstIndex);
	if (State)
	{
		return FTransform(State->Rotation, State->Location, State->Scale);
	}

	// Fallback to current state
	return FTransform(
		SplineComponent->GetQuaternionAtSplinePoint(FirstIndex, ESplineCoordinateSpace::World),
		SplineComponent->GetLocationAtSplinePoint(FirstIndex, ESplineCoordinateSpace::World),
		SplineComponent->GetScaleAtSplinePoint(FirstIndex)
	);
}

FVector FSplinePointTransformHandler::ComputeAverageLocalAxis(EAxis::Type Axis) const
{
	if (!SplineComponent.IsValid() || SelectedPointIndices.Num() == 0)
	{
		return FVector::ZeroVector;
	}

	// Accumulate axis vectors from each selected spline point
	FVector AccumulatedAxis = FVector::ZeroVector;
	int32 Count = 0;

	for (int32 Index : SelectedPointIndices)
	{
		if (const FPointState* State = InitialPointStates.Find(Index))
		{
			const FQuat Rotation = State->Rotation;
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

	if (Count == 0)
	{
		return FVector::ZeroVector;
	}

	return (AccumulatedAxis / Count).GetSafeNormal();
}

void FSplinePointTransformHandler::CaptureInitialState()
{
	InitialPointStates.Empty();

	if (!SplineComponent.IsValid())
	{
		return;
	}

	for (int32 Index : SelectedPointIndices)
	{
		FPointState State;
		State.Location = SplineComponent->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World);
		State.Rotation = SplineComponent->GetQuaternionAtSplinePoint(Index, ESplineCoordinateSpace::World);
		State.Scale = SplineComponent->GetScaleAtSplinePoint(Index);
		State.ArriveTangent = SplineComponent->GetArriveTangentAtSplinePoint(Index, ESplineCoordinateSpace::World);
		State.LeaveTangent = SplineComponent->GetLeaveTangentAtSplinePoint(Index, ESplineCoordinateSpace::World);

		InitialPointStates.Add(Index, State);
	}
}

void FSplinePointTransformHandler::RestoreInitialState()
{
	if (!SplineComponent.IsValid())
	{
		return;
	}

	for (const auto& Pair : InitialPointStates)
	{
		const int32 Index = Pair.Key;
		const FPointState& State = Pair.Value;

		SplineComponent->SetLocationAtSplinePoint(Index, State.Location, ESplineCoordinateSpace::World, false);
		SplineComponent->SetRotationAtSplinePoint(Index, State.Rotation.Rotator(), ESplineCoordinateSpace::World, false);
		SplineComponent->SetScaleAtSplinePoint(Index, State.Scale, false);
		SplineComponent->SetTangentsAtSplinePoint(Index, State.ArriveTangent, State.LeaveTangent, ESplineCoordinateSpace::World, false);
	}

	SplineComponent->UpdateSpline();

	// Notify owning actor that movement is complete (restored to original state)
	if (AActor* Owner = SplineComponent->GetOwner())
	{
		Owner->PostEditMove(true);
	}
}

void FSplinePointTransformHandler::ApplyTransformAroundPivot(const FTransform& InitialPivot,
                                                             const FTransform& NewPivotTransform,
                                                             TOptional<EAxis::Type> LocalScaleAxis)
{
	if (!SplineComponent.IsValid())
	{
		return;
	}

	// Calculate deltas
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

	for (int32 Index : SelectedPointIndices)
	{
		const FPointState* InitialState = InitialPointStates.Find(Index);
		if (!InitialState)
		{
			continue;
		}

		FVector NewLocation;
		FQuat NewRotation;
		FVector NewArriveTangent;
		FVector NewLeaveTangent;

		if (LocalScaleAxis.IsSet())
		{
			// Local-axis scale: apply scale in the point's local coordinate system
			const FQuat PointRotation = InitialState->Rotation;

			// Calculate position: transform offset to local space, scale, transform back
			const FVector WorldOffset = InitialState->Location - PivotLocation;
			FVector LocalOffset = PointRotation.UnrotateVector(WorldOffset);

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

			const FVector NewWorldOffset = PointRotation.RotateVector(LocalOffset);
			NewLocation = PivotLocation + DeltaTranslation + NewWorldOffset;

			// Rotation is unchanged for scale operations
			NewRotation = DeltaRotation * InitialState->Rotation;

			// Scale tangents in local space
			FVector LocalArriveTangent = PointRotation.UnrotateVector(InitialState->ArriveTangent);
			FVector LocalLeaveTangent = PointRotation.UnrotateVector(InitialState->LeaveTangent);

			switch (LocalScaleAxis.GetValue())
			{
			case EAxis::X:
				LocalArriveTangent.X *= LocalScaleFactor;
				LocalLeaveTangent.X *= LocalScaleFactor;
				break;
			case EAxis::Y:
				LocalArriveTangent.Y *= LocalScaleFactor;
				LocalLeaveTangent.Y *= LocalScaleFactor;
				break;
			case EAxis::Z:
				LocalArriveTangent.Z *= LocalScaleFactor;
				LocalLeaveTangent.Z *= LocalScaleFactor;
				break;
			default:
				break;
			}

			NewArriveTangent = DeltaRotation.RotateVector(PointRotation.RotateVector(LocalArriveTangent));
			NewLeaveTangent = DeltaRotation.RotateVector(PointRotation.RotateVector(LocalLeaveTangent));
		}
		else
		{
			// Standard world-space transform
			const FVector InitialRelativeToPivot = InitialState->Location - PivotLocation;
			const FVector RotatedOffset = DeltaRotation.RotateVector(InitialRelativeToPivot);
			const FVector ScaledOffset = RotatedOffset * DeltaScale;

			NewLocation = PivotLocation + DeltaTranslation + ScaledOffset;
			NewRotation = DeltaRotation * InitialState->Rotation;

			// Apply scale to tangents (for scale mode)
			NewArriveTangent = DeltaRotation.RotateVector(InitialState->ArriveTangent) * DeltaScale.X;
			NewLeaveTangent = DeltaRotation.RotateVector(InitialState->LeaveTangent) * DeltaScale.X;
		}

		// Set the new values (defer spline update until all points are modified)
		SplineComponent->SetLocationAtSplinePoint(Index, NewLocation, ESplineCoordinateSpace::World, false);
		SplineComponent->SetRotationAtSplinePoint(Index, NewRotation.Rotator(), ESplineCoordinateSpace::World, false);
		SplineComponent->SetTangentsAtSplinePoint(Index, NewArriveTangent, NewLeaveTangent, ESplineCoordinateSpace::World, false);
	}

	// Update spline once after all points are modified
	SplineComponent->UpdateSpline();

	// Notify owning actor of movement (for dependent systems like construction scripts)
	if (AActor* Owner = SplineComponent->GetOwner())
	{
		Owner->PostEditMove(false);
	}
}

void FSplinePointTransformHandler::SetDirectTransform(const FVector* Location, const FRotator* Rotation, const FVector* Scale)
{
	if (!SplineComponent.IsValid())
	{
		return;
	}

	for (int32 Index : SelectedPointIndices)
	{
		if (Location)
		{
			SplineComponent->SetLocationAtSplinePoint(Index, *Location, ESplineCoordinateSpace::World, false);
		}
		if (Rotation)
		{
			SplineComponent->SetRotationAtSplinePoint(Index, *Rotation, ESplineCoordinateSpace::World, false);
		}
		if (Scale)
		{
			SplineComponent->SetScaleAtSplinePoint(Index, *Scale, false);
		}
	}

	SplineComponent->UpdateSpline();

	// Notify owning actor of movement (for dependent systems like construction scripts)
	if (AActor* Owner = SplineComponent->GetOwner())
	{
		Owner->PostEditMove(false);
	}
}

int32 FSplinePointTransformHandler::BeginTransaction(const FText& Description)
{
	if (!GEditor || !SplineComponent.IsValid())
	{
		return -1;
	}

	int32 TransactionIndex = GEditor->BeginTransaction(TEXT(""), Description, nullptr);

	// Mark spline component for undo
	SplineComponent->Modify();

	return TransactionIndex;
}

void FSplinePointTransformHandler::EndTransaction()
{
	if (GEditor)
	{
		// Notify owning actor that movement has finished
		if (SplineComponent.IsValid())
		{
			if (AActor* Owner = SplineComponent->GetOwner())
			{
				Owner->PostEditMove(true);
			}
		}

		GEditor->EndTransaction();
	}
}

void FSplinePointTransformHandler::CancelTransaction(int32 TransactionIndex)
{
	if (GEditor && TransactionIndex >= 0)
	{
		GEditor->CancelTransaction(TransactionIndex);
	}
}
