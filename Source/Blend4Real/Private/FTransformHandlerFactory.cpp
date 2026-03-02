// Copyright 2025-Present - Nehon (Rémy Bouquet). All Rights Reserved.

#include "FTransformHandlerFactory.h"
#include "IBlend4RealTransformHandler.h"
#include "FActorTransformHandler.h"
#include "FComponentTransformHandler.h"
#include "FSCSTransformHandler.h"
#include "FSplinePointTransformHandler.h"
#include "FControlRigShapeTransformHandler.h"
#include "FControlRigPreviewTransformHandler.h"
#include "FBoneTransformHandler.h"
#include "FEditSkeletonTransformHandler.h"
#include "Blend4RealUtils.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "BlueprintEditorModule.h"
#include "BlueprintEditor.h"
#include "Features/IModularFeatures.h"
#include "SplineDetailsProvider.h"
#include "Components/SplineComponent.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "EngineUtils.h"
#include "EditorModeManager.h"
#include "InteractiveToolManager.h"
#include "SkeletalMesh/SkeletonEditingTool.h"
#include "Subsystems/AssetEditorSubsystem.h"

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
	 * Try to get an IPersonaPreviewScene from a viewport client's preview scene.
	 * Returns nullptr if the preview scene is not a Persona preview scene.
	 */
	IPersonaPreviewScene* GetPersonaPreviewScene(FEditorViewportClient* ViewportClient)
	{
		if (!ViewportClient)
		{
			return nullptr;
		}

		FPreviewScene* RawPreviewScene = ViewportClient->GetPreviewScene();
		if (!RawPreviewScene)
		{
			return nullptr;
		}

		// Verify this is a Persona preview scene by checking for UDebugSkelMeshComponent
		// Only Persona-based editors (Animation, Skeleton, IK Rig) use these
		UWorld* PreviewWorld = RawPreviewScene->GetWorld();
		if (!PreviewWorld)
		{
			return nullptr;
		}

		for (TActorIterator<AActor> It(PreviewWorld); It; ++It)
		{
			if (It->FindComponentByClass<UDebugSkelMeshComponent>())
			{
				return static_cast<IPersonaPreviewScene*>(RawPreviewScene);
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

	/**
	 * Get the active USkeletonEditingTool when "Edit Skeleton" mode is active.
	 * Returns nullptr if the mode is not active or no skeleton editing tool is running.
	 */
	USkeletonEditingTool* GetActiveSkeletonEditingTool(FEditorViewportClient* ViewportClient)
	{
		if (!ViewportClient)
		{
			return nullptr;
		}

		FEditorModeTools* ModeTools = ViewportClient->GetModeTools();
		if (!ModeTools || !ModeTools->IsModeActive(FEditorModeID("SkeletalMeshModelingToolsEditorMode")))
		{
			return nullptr;
		}

		UEdMode* Mode = ModeTools->GetActiveScriptableMode(FEditorModeID("SkeletalMeshModelingToolsEditorMode"));
		if (!Mode)
		{
			return nullptr;
		}

		UInteractiveToolManager* ToolManager = Mode->GetToolManager();
		if (!ToolManager)
		{
			return nullptr;
		}

		UInteractiveTool* ActiveTool = ToolManager->GetActiveTool(EToolSide::Left);
		return Cast<USkeletonEditingTool>(ActiveTool);
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

		// Priority 0.5: Control Rig shape actors
		if (FControlRigShapeTransformHandler::HasSelectedShapeActors())
		{
			return MakeShared<FControlRigShapeTransformHandler>();
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

	// Other editor viewports (e.g., Control Rig Editor, Animation Editor, IK Rig Editor)
	if (Blend4RealUtils::IsMouseOverViewport(MousePosition))
	{
		FEditorViewportClient* ViewportClient = Blend4RealUtils::GetFocusedViewportClient();
		if (ViewportClient)
		{
			UWorld* ViewportWorld = ViewportClient->GetWorld();
			if (ViewportWorld && ViewportWorld != Blend4RealUtils::GetEditorWorld())
			{
				// Priority 0: Control Rig shape actors in preview scenes
				if (FControlRigPreviewTransformHandler::HasSelectedShapeActors(ViewportWorld))
				{
					return MakeShared<FControlRigPreviewTransformHandler>(ViewportWorld);
				}

				// Priority 0: Edit Skeleton mode (completely different transform pipeline —
				// directly edits RawRefBonePose, bypasses animation system entirely)
				USkeletonEditingTool* SkeletonTool = GetActiveSkeletonEditingTool(ViewportClient);
				if (SkeletonTool && SkeletonTool->GetSelection().Num() > 0)
				{
					return MakeShared<FEditSkeletonTransformHandler>(SkeletonTool, ViewportWorld);
				}

				// Priority 1: Bone selection in Persona-based editors
				IPersonaPreviewScene* PersonaScene = GetPersonaPreviewScene(ViewportClient);
				if (PersonaScene)
				{
					if (PersonaScene->GetSelectedBoneIndex() != INDEX_NONE)
					{
						return MakeShared<FBoneTransformHandler>(PersonaScene);
					}
				}
			}
		}
	}

	// TODO: Add more viewport types here:
	// - Static Mesh Editor (sockets)
	// - Skeleton Editor (sockets)

	// No supported viewport focused
	return nullptr;
}
