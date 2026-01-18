#include "FSCSTransformHandler.h"
#include "BlueprintEditor.h"
#include "SSubobjectEditor.h"
#include "Editor.h"
#include "Components/SceneComponent.h"

FSCSTransformHandler::FSCSTransformHandler(TWeakPtr<FBlueprintEditor> InBlueprintEditor)
	: BlueprintEditorPtr(InBlueprintEditor)
{
}

USceneComponent* FSCSTransformHandler::GetTemplateComponent(const TSharedPtr<FSubobjectEditorTreeNode>& Node) const
{
	if (!Node.IsValid())
	{
		return nullptr;
	}

	const FSubobjectData* Data = Node->GetDataSource();
	if (!Data)
	{
		return nullptr;
	}

	const TSharedPtr<FBlueprintEditor> Editor = BlueprintEditorPtr.Pin();
	if (!Editor.IsValid())
	{
		return nullptr;
	}

	UBlueprint* Blueprint = Editor->GetBlueprintObj();
	if (!Blueprint)
	{
		return nullptr;
	}

	// Note: FSubobjectData's mutable accessors are private (for use by USubobjectDataSubsystem only).
	// The engine's FSCSEditorViewportClient uses the same const_cast pattern with a TODO to clean it up.
	// See: Engine/Source/Editor/Kismet/Private/SCSEditorViewportClient.cpp line ~570
	return const_cast<USceneComponent*>(Data->GetObjectForBlueprint<USceneComponent>(Blueprint));
}

USceneComponent* FSCSTransformHandler::GetPreviewInstance(const TSharedPtr<FSubobjectEditorTreeNode>& Node) const
{
	if (!Node.IsValid())
	{
		return nullptr;
	}

	const FSubobjectData* Data = Node->GetDataSource();
	if (!Data)
	{
		return nullptr;
	}

	TSharedPtr<FBlueprintEditor> Editor = BlueprintEditorPtr.Pin();
	if (!Editor.IsValid())
	{
		return nullptr;
	}

	AActor* PreviewActor = Editor->GetPreviewActor();
	if (!PreviewActor)
	{
		return nullptr;
	}

	// Note: FSubobjectData's mutable accessors are private (for use by USubobjectDataSubsystem only).
	// The engine's FSCSEditorViewportClient uses the same const_cast pattern with a TODO to clean it up.
	// See: Engine/Source/Editor/Kismet/Private/SCSEditorViewportClient.cpp line ~571
	return const_cast<USceneComponent*>(Cast<USceneComponent>(Data->FindComponentInstanceInActor(PreviewActor)));
}

bool FSCSTransformHandler::IsTransformableNode(const TSharedPtr<FSubobjectEditorTreeNode>& Node) const
{
	if (!Node.IsValid())
	{
		return false;
	}

	const FSubobjectData* Data = Node->GetDataSource();
	if (!Data)
	{
		return false;
	}

	// Skip root components - they define the actor's origin
	// Note: Engine's FSCSEditorViewportClient also skips root (line ~559)
	if (Data->IsRootComponent())
	{
		return false;
	}

	// Note: We do NOT skip inherited components - the engine allows transforming them.
	// Moving an inherited component creates an override in the child Blueprint,
	// it doesn't modify the parent class.

	// Must be a SceneComponent
	const USceneComponent* Template = GetTemplateComponent(Node);
	return Template != nullptr;
}

TArray<TSharedPtr<FSubobjectEditorTreeNode>> FSCSTransformHandler::GetTransformableSelectedNodes() const
{
	TArray<TSharedPtr<FSubobjectEditorTreeNode>> Result;

	TSharedPtr<FBlueprintEditor> Editor = BlueprintEditorPtr.Pin();
	if (!Editor.IsValid())
	{
		return Result;
	}

	TArray<TSharedPtr<FSubobjectEditorTreeNode>> SelectedNodes = Editor->GetSelectedSubobjectEditorTreeNodes();
	for (const TSharedPtr<FSubobjectEditorTreeNode>& Node : SelectedNodes)
	{
		if (IsTransformableNode(Node))
		{
			Result.Add(Node);
		}
	}

	return Result;
}

bool FSCSTransformHandler::HasSelection() const
{
	return GetTransformableSelectedNodes().Num() > 0;
}

int32 FSCSTransformHandler::GetSelectionCount() const
{
	return GetTransformableSelectedNodes().Num();
}

FTransform FSCSTransformHandler::ComputeSelectionPivot() const
{
	TArray<TSharedPtr<FSubobjectEditorTreeNode>> Nodes = GetTransformableSelectedNodes();
	if (Nodes.Num() == 0)
	{
		return FTransform::Identity;
	}

	FVector Center = FVector::ZeroVector;
	int32 Count = 0;

	for (const TSharedPtr<FSubobjectEditorTreeNode>& Node : Nodes)
	{
		// Use preview instance for world position (template may have relative transform only)
		if (USceneComponent* Instance = GetPreviewInstance(Node))
		{
			Center += Instance->GetComponentLocation();
			Count++;
		}
	}

	if (Count > 0)
	{
		Center /= Count;
	}

	return FTransform(FQuat::Identity, Center, FVector::OneVector);
}

FTransform FSCSTransformHandler::GetFirstSelectedItemTransform() const
{
	TArray<TSharedPtr<FSubobjectEditorTreeNode>> Nodes = GetTransformableSelectedNodes();
	if (Nodes.Num() == 0)
	{
		return FTransform::Identity;
	}

	// Use preview instance for world transform
	if (USceneComponent* Instance = GetPreviewInstance(Nodes[0]))
	{
		return Instance->GetComponentTransform();
	}

	return FTransform::Identity;
}

FVector FSCSTransformHandler::ComputeAverageLocalAxis(EAxis::Type Axis) const
{
	TArray<TSharedPtr<FSubobjectEditorTreeNode>> Nodes = GetTransformableSelectedNodes();
	if (Nodes.Num() == 0)
	{
		return FVector::ZeroVector;
	}

	// Accumulate axis vectors from each selected node
	FVector AccumulatedAxis = FVector::ZeroVector;
	int32 Count = 0;

	for (const TSharedPtr<FSubobjectEditorTreeNode>& Node : Nodes)
	{
		const FSubobjectData* Data = Node->GetDataSource();
		if (!Data)
		{
			continue;
		}

		if (const FTransform* Transform = InitialTransforms.Find(Data->GetHandle()))
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

	if (Count == 0)
	{
		return FVector::ZeroVector;
	}

	return (AccumulatedAxis / Count).GetSafeNormal();
}

void FSCSTransformHandler::CaptureInitialState()
{
	InitialTransforms.Empty();

	TArray<TSharedPtr<FSubobjectEditorTreeNode>> Nodes = GetTransformableSelectedNodes();
	for (const TSharedPtr<FSubobjectEditorTreeNode>& Node : Nodes)
	{
		const FSubobjectData* Data = Node->GetDataSource();
		if (!Data)
		{
			continue;
		}

		// Store world transform from preview instance for computing deltas
		if (USceneComponent* Instance = GetPreviewInstance(Node))
		{
			InitialTransforms.Add(Data->GetHandle(), Instance->GetComponentTransform());
		}
	}
}

void FSCSTransformHandler::RestoreInitialState()
{
	TArray<TSharedPtr<FSubobjectEditorTreeNode>> Nodes = GetTransformableSelectedNodes();
	for (const TSharedPtr<FSubobjectEditorTreeNode>& Node : Nodes)
	{
		const FSubobjectData* Data = Node->GetDataSource();
		if (!Data)
		{
			continue;
		}

		const FTransform* Original = InitialTransforms.Find(Data->GetHandle());
		if (!Original)
		{
			continue;
		}

		// Restore both template and preview instance
		if (USceneComponent* Template = GetTemplateComponent(Node))
		{
			Template->SetWorldTransform(*Original);
		}
		if (USceneComponent* Instance = GetPreviewInstance(Node))
		{
			Instance->SetWorldTransform(*Original);
		}
	}
}

void FSCSTransformHandler::ApplyTransformAroundPivot(const FTransform& InitialPivot,
                                                     const FTransform& NewPivotTransform)
{
	TArray<TSharedPtr<FSubobjectEditorTreeNode>> Nodes = GetTransformableSelectedNodes();
	if (Nodes.Num() == 0)
	{
		return;
	}

	// Calculate the delta between initial and new pivot transforms
	const FVector DeltaTranslation = NewPivotTransform.GetLocation() - InitialPivot.GetLocation();
	const FQuat DeltaRotation = NewPivotTransform.GetRotation() * InitialPivot.GetRotation().Inverse();
	const FVector DeltaScale = NewPivotTransform.GetScale3D() / InitialPivot.GetScale3D();

	const FVector PivotLocation = InitialPivot.GetLocation();

	for (const TSharedPtr<FSubobjectEditorTreeNode>& Node : Nodes)
	{
		const FSubobjectData* Data = Node->GetDataSource();
		if (!Data)
		{
			continue;
		}

		const FTransform* InitialComponentTransform = InitialTransforms.Find(Data->GetHandle());
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
			// Apply to BOTH template and preview instance
			if (USceneComponent* Template = GetTemplateComponent(Node))
			{
				Template->SetWorldTransform(NewTransform);
			}
			if (USceneComponent* Instance = GetPreviewInstance(Node))
			{
				Instance->SetWorldTransform(NewTransform);
			}
		}
	}
}

void FSCSTransformHandler::SetDirectTransform(const FVector* Location, const FRotator* Rotation, const FVector* Scale)
{
	TArray<TSharedPtr<FSubobjectEditorTreeNode>> Nodes = GetTransformableSelectedNodes();
	for (const TSharedPtr<FSubobjectEditorTreeNode>& Node : Nodes)
	{
		USceneComponent* Template = GetTemplateComponent(Node);
		USceneComponent* Instance = GetPreviewInstance(Node);

		// Build transform from current + overrides
		FTransform CurrentTransform = Instance
			                              ? Instance->GetComponentTransform()
			                              : (Template ? Template->GetComponentTransform() : FTransform::Identity);

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

		// Apply to both template and preview instance
		if (Template)
		{
			Template->SetWorldTransform(CurrentTransform);
		}
		if (Instance)
		{
			Instance->SetWorldTransform(CurrentTransform);
		}
	}
}

int32 FSCSTransformHandler::BeginTransaction(const FText& Description)
{
	if (!GEditor)
	{
		return -1;
	}

	int32 TransactionIndex = GEditor->BeginTransaction(TEXT(""), Description, nullptr);

	// Mark all template components as modified for undo
	TArray<TSharedPtr<FSubobjectEditorTreeNode>> Nodes = GetTransformableSelectedNodes();
	for (const TSharedPtr<FSubobjectEditorTreeNode>& Node : Nodes)
	{
		if (USceneComponent* Template = GetTemplateComponent(Node))
		{
			Template->Modify();
		}
	}

	return TransactionIndex;
}

void FSCSTransformHandler::EndTransaction()
{
	if (GEditor)
	{
		GEditor->EndTransaction();
	}
}

void FSCSTransformHandler::CancelTransaction(int32 TransactionIndex)
{
	if (GEditor && TransactionIndex >= 0)
	{
		GEditor->CancelTransaction(TransactionIndex);
	}
}

UWorld* FSCSTransformHandler::GetVisualizationWorld() const
{
	TSharedPtr<FBlueprintEditor> Editor = BlueprintEditorPtr.Pin();
	if (!Editor.IsValid())
	{
		return nullptr;
	}

	AActor* PreviewActor = Editor->GetPreviewActor();
	if (!PreviewActor)
	{
		return nullptr;
	}

	return PreviewActor->GetWorld();
}
