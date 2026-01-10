#include "FTransformHandlerFactory.h"
#include "IBlend4RealTransformHandler.h"
#include "FActorTransformHandler.h"
#include "FComponentTransformHandler.h"
#include "FSCSTransformHandler.h"
#include "FSplinePointTransformHandler.h"
#include "Blend4RealUtils.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "BlueprintEditorModule.h"
#include "BlueprintEditor.h"
#include "Features/IModularFeatures.h"
#include "SplineDetailsProvider.h"
#include "Components/SplineComponent.h"

namespace
{
	/**
	 * Try to create a spline point transform handler if spline control points are selected.
	 * Returns nullptr if no spline points are selected.
	 */
	TSharedPtr<IBlend4RealTransformHandler> TryCreateSplinePointHandler()
	{
		// Get all spline details providers (visualizers that can provide selection state)
		TArray<ISplineDetailsProvider*> Providers = IModularFeatures::Get()
			.GetModularFeatureImplementations<ISplineDetailsProvider>(ISplineDetailsProvider::GetModularFeatureName());

		for (ISplineDetailsProvider* Provider : Providers)
		{
			if (Provider && Provider->GetSelectedKeys().Num() > 0)
			{
				USplineComponent* SplineComp = Provider->GetEditedSplineComponent();
				if (SplineComp)
				{
					return MakeShared<FSplinePointTransformHandler>(SplineComp, Provider->GetSelectedKeys());
				}
			}
		}

		return nullptr;
	}

	/**
	 * Find the Blueprint editor that owns the SSCSEditorViewport at the given mouse position.
	 * Returns nullptr if no matching editor is found.
	 */
	TWeakPtr<FBlueprintEditor> FindBlueprintEditorAtPosition(const FVector2D& MousePosition)
	{
		// Get the Blueprint editor module
		FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");

		// Get all open Blueprint editors
		TArray<TSharedRef<IBlueprintEditor>> BlueprintEditors = BlueprintEditorModule.GetBlueprintEditors();

		for (const TSharedRef<IBlueprintEditor>& Editor : BlueprintEditors)
		{
			// Cast to FBlueprintEditor to access the subobject viewport
			TSharedRef<FBlueprintEditor> BlueprintEditor = StaticCastSharedRef<FBlueprintEditor>(Editor);

			// Check if this editor has a valid preview actor (indicates SCS editor is active)
			if (BlueprintEditor->GetPreviewActor() != nullptr)
			{
				// Check if this editor has any selected nodes
				// This helps confirm we're in the right editor
				TArray<TSharedPtr<FSubobjectEditorTreeNode>> SelectedNodes = BlueprintEditor->GetSelectedSubobjectEditorTreeNodes();

				// For now, return the first editor that has a preview actor
				// TODO: More precise matching by checking if the viewport widget matches
				return BlueprintEditor;
			}
		}

		return nullptr;
	}
}

TSharedPtr<IBlend4RealTransformHandler> FTransformHandlerFactory::CreateHandler()
{
	if (!GEditor)
	{
		return nullptr;
	}

	// Get current mouse position to check which viewport type we're over
	const FVector2D MousePosition = FSlateApplication::Get().GetCursorPos();

	// Level Editor: Check selection state to determine handler type
	if (Blend4RealUtils::IsMouseOverViewport(MousePosition, FName("SLevelViewport")))
	{
		// Priority 0: Spline control points (most specific selection)
		if (TSharedPtr<IBlend4RealTransformHandler> SplineHandler = TryCreateSplinePointHandler())
		{
			return SplineHandler;
		}

		// Priority 1: Components (more specific selection)
		USelection* SelectedComponents = GEditor->GetSelectedComponents();
		if (SelectedComponents && SelectedComponents->Num() > 0)
		{
			bool bHasSceneComponent = false;
			for (FSelectionIterator It(*SelectedComponents); It; ++It)
			{
				if (const USceneComponent* Obj = Cast<USceneComponent>(*It))
				{
					// Only SceneComponents can be transformed
					bHasSceneComponent = true;
					break;
				}
			}
			if (bHasSceneComponent)
			{
				return MakeShared<FComponentTransformHandler>();
			}
		}

		// Priority 2: Actors
		USelection* SelectedActors = GEditor->GetSelectedActors();
		if (SelectedActors && SelectedActors->Num() > 0)
		{
			return MakeShared<FActorTransformHandler>();
		}

		// Nothing selected
		return nullptr;
	}

	// SCS Editor: Blueprint component editing
	if (Blend4RealUtils::IsMouseOverViewport(MousePosition, FName("SSCSEditorViewport")))
	{
		TWeakPtr<FBlueprintEditor> BlueprintEditor = FindBlueprintEditorAtPosition(MousePosition);
		if (BlueprintEditor.IsValid())
		{
			TSharedPtr<FSCSTransformHandler> Handler = MakeShared<FSCSTransformHandler>(BlueprintEditor);
			// Only return if there's actually something selected to transform
			if (Handler->HasSelection())
			{
				return Handler;
			}
		}
		return nullptr;
	}

	// TODO: Add more viewport types here:
	// - Animation Editor (bones)
	// - Static Mesh Editor (sockets)
	// - Skeleton Editor (sockets)

	// No supported viewport focused
	return nullptr;
}
