#pragma once

#include "CoreMinimal.h"
#include "IBlend4RealTransformHandler.h"
#include "SubobjectData.h"

class FBlueprintEditor;
class FSubobjectEditorTreeNode;
class USceneComponent;

/**
 * Transform handler for components in the Blueprint SCS (Component) Editor.
 * Uses FBlueprintEditor::GetSelectedSubobjectEditorTreeNodes() for selection.
 *
 * Key differences from FComponentTransformHandler:
 * - Selection comes from the subobject editor tree, not GEditor
 * - Must write transforms to both template (for persistence) and preview instance (for visualization)
 * - Uses the preview scene world for visualization
 */
class FSCSTransformHandler : public IBlend4RealTransformHandler
{
public:
	explicit FSCSTransformHandler(TWeakPtr<FBlueprintEditor> InBlueprintEditor);
	virtual ~FSCSTransformHandler() override = default;

	// === Selection Queries ===
	virtual bool HasSelection() const override;
	virtual int32 GetSelectionCount() const override;

	// === Transform Data ===
	virtual FTransform ComputeSelectionPivot() const override;
	virtual FTransform GetFirstSelectedItemTransform() const override;

	// === State Management ===
	virtual void CaptureInitialState() override;
	virtual void RestoreInitialState() override;

	// === Transform Application ===
	virtual void ApplyTransformAroundPivot(const FTransform& InitialPivot, const FTransform& NewPivotTransform) override;
	virtual void SetDirectTransform(const FVector* Location, const FRotator* Rotation, const FVector* Scale) override;

	// === Transaction Handling ===
	virtual int32 BeginTransaction(const FText& Description) override;
	virtual void EndTransaction() override;
	virtual void CancelTransaction(int32 TransactionIndex) override;

	// === Visualization Context ===
	virtual UWorld* GetVisualizationWorld() const override;

private:
	/** Get the template component (editable in Blueprint) from a tree node */
	USceneComponent* GetTemplateComponent(const TSharedPtr<FSubobjectEditorTreeNode>& Node) const;

	/** Get the preview instance component (visible in viewport) from a tree node */
	USceneComponent* GetPreviewInstance(const TSharedPtr<FSubobjectEditorTreeNode>& Node) const;

	/** Check if a node represents a transformable component (not root, not inherited) */
	bool IsTransformableNode(const TSharedPtr<FSubobjectEditorTreeNode>& Node) const;

	/** Get all selected nodes that are transformable */
	TArray<TSharedPtr<FSubobjectEditorTreeNode>> GetTransformableSelectedNodes() const;

	/** Weak reference to the Blueprint editor */
	TWeakPtr<FBlueprintEditor> BlueprintEditorPtr;

	/** Stored initial transforms keyed by subobject data handle */
	TMap<FSubobjectDataHandle, FTransform> InitialTransforms;
};
