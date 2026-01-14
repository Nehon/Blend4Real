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

void FSplinePointTransformHandler::ApplyTransformAroundPivot(const FTransform& InitialPivot, const FTransform& NewPivotTransform)
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

	for (int32 Index : SelectedPointIndices)
	{
		const FPointState* InitialState = InitialPointStates.Find(Index);
		if (!InitialState)
		{
			continue;
		}

		// Calculate position relative to pivot
		const FVector InitialRelativeToPivot = InitialState->Location - PivotLocation;

		// Apply rotation around pivot
		const FVector RotatedOffset = DeltaRotation.RotateVector(InitialRelativeToPivot);

		// Apply scale around pivot
		const FVector ScaledOffset = RotatedOffset * DeltaScale;

		// Calculate new world position
		const FVector NewLocation = PivotLocation + DeltaTranslation + ScaledOffset;

		// Apply rotation to the point's own rotation
		const FQuat NewRotation = DeltaRotation * InitialState->Rotation;

		// Apply scale to tangents (for scale mode)
		const FVector NewArriveTangent = DeltaRotation.RotateVector(InitialState->ArriveTangent) * DeltaScale.X;
		const FVector NewLeaveTangent = DeltaRotation.RotateVector(InitialState->LeaveTangent) * DeltaScale.X;

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
