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
			}
		}
	}
}

void FComponentTransformHandler::ApplyTransformAroundPivot(const FTransform& InitialPivot,
                                                           const FTransform& NewPivotTransform)
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

	for (FSelectionIterator It(*Selection); It; ++It)
	{
		if (USceneComponent* Component = Cast<USceneComponent>(*It))
		{
			const FTransform* InitialComponentTransform = InitialTransforms.Find(Component->GetUniqueID());
			if (!InitialComponentTransform)
			{
				continue;
			}

			// Calculate the component's position relative to pivot
			const FVector InitialRelativeToPivot = InitialComponentTransform->GetLocation() - PivotLocation;

			// Apply rotation around pivot to get new position offset
			const FVector RotatedOffset = DeltaRotation.RotateVector(InitialRelativeToPivot);

			// Apply scale around pivot
			const FVector ScaledOffset = RotatedOffset * DeltaScale;

			// Calculate new world position
			const FVector NewLocation = PivotLocation + DeltaTranslation + ScaledOffset;

			// Apply rotation to the component's own rotation
			const FQuat NewRotation = DeltaRotation * InitialComponentTransform->GetRotation();

			// Apply scale to the component's own scale
			const FVector NewScale = InitialComponentTransform->GetScale3D() * DeltaScale;

			// Build new transform
			FTransform NewTransform(NewRotation, NewLocation, NewScale);

			if (NewTransform.IsValid())
			{
				Component->SetWorldTransform(NewTransform);
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
