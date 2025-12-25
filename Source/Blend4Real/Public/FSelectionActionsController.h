#pragma once

#include "CoreMinimal.h"

class FTransformController;

/**
 * Handles selection-based actions: delete and duplicate
 */
class FSelectionActionsController
{
public:
	/**
	 * Constructor
	 * @param InTransformController - Reference to transform controller for duplicate+grab
	 */
	explicit FSelectionActionsController(TSharedPtr<FTransformController> InTransformController);

	/** Duplicate selected actors and immediately enter grab mode */
	void DuplicateSelectedAndGrab() const;

	/** Delete all selected actors */
	void DeleteSelected();

private:
	TWeakPtr<FTransformController> TransformController;
};
