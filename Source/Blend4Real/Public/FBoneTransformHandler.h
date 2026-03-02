// Copyright 2025-Present - Nehon (Rémy Bouquet). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IBlend4RealTransformHandler.h"

class IPersonaPreviewScene;
class UDebugSkelMeshComponent;
class UAnimPreviewInstance;

/**
 * Transform handler for bones in Persona-based editors (Animation Editor, Skeleton Editor).
 * Uses FAnimNode_ModifyBone (additive, bone space) to apply preview transforms.
 */
class FBoneTransformHandler : public IBlend4RealTransformHandler
{
public:
	explicit FBoneTransformHandler(IPersonaPreviewScene* InPreviewScene);
	virtual ~FBoneTransformHandler() override = default;

	// === IBlend4RealTransformHandler ===

	virtual bool HasSelection() const override;
	virtual int32 GetSelectionCount() const override;

	virtual FTransform ComputeSelectionPivot() const override;
	virtual FTransform GetFirstSelectedItemTransform() const override;
	virtual FVector ComputeAverageLocalAxis(EAxis::Type Axis) const override;

	virtual void CaptureInitialState() override;
	virtual void RestoreInitialState() override;

	virtual void ApplyTransformAroundPivot(
		const FTransform& InitialPivot,
		const FTransform& NewPivotTransform,
		TOptional<EAxis::Type> LocalScaleAxis = TOptional<EAxis::Type>()) override;
	virtual void SetDirectTransform(const FVector* Location, const FRotator* Rotation, const FVector* Scale) override;

	virtual int32 BeginTransaction(const FText& Description) override;
	virtual void EndTransaction() override;
	virtual void CancelTransaction(int32 TransactionIndex) override;

	virtual UWorld* GetVisualizationWorld() const override;

private:
	/** The Persona preview scene this handler operates in */
	IPersonaPreviewScene* PreviewScene;

	/** Weak reference to the preview world for validity checking */
	TWeakObjectPtr<UWorld> PreviewWorld;

	/** Per-bone initial state for capture/restore */
	struct FBoneState
	{
		int32 BoneIndex;
		FName BoneName;
		FTransform InitialWorldTransform;
		FVector InitialSkelControlTranslation;
		FRotator InitialSkelControlRotation;
		FVector InitialSkelControlScale;
		/** Caches the world-to-bone-space conversion transform (SkelControlTM * WorldTM^-1) */
		FTransform BaseTM;
	};

	TArray<FBoneState> InitialStates;

	/** Get mesh component and preview instance, returns false if invalid */
	bool GetPreviewComponents(UDebugSkelMeshComponent*& OutMeshComp, UAnimPreviewInstance*& OutPreviewInstance) const;
};
