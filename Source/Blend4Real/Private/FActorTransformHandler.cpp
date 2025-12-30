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

	if (AActor* Actor = GEditor->GetSelectedActors()->GetTop<AActor>())
	{
		return Actor->GetActorTransform();
	}

	return FTransform::Identity;
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
			}
		}
	}
}

void FActorTransformHandler::ApplyTransformAroundPivot(const FTransform& InitialPivot,
                                                       const FTransform& NewPivotTransform)
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
			const FTransform* InitialActorTransform = InitialTransforms.Find(Actor->GetUniqueID());
			if (!InitialActorTransform)
			{
				continue;
			}

			// Transform actor relative to pivot:
			// 1. Remove initial pivot transform
			// 2. Apply new pivot transform
			FTransform ActorTransform = *InitialActorTransform * InitialPivot.Inverse();
			ActorTransform = ActorTransform * NewPivotTransform;

			if (!ActorTransform.ContainsNaN())
			{
				Actor->SetActorTransform(ActorTransform, false, nullptr, ETeleportType::None);
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
