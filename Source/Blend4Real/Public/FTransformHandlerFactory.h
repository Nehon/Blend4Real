#pragma once

#include "CoreMinimal.h"

class IBlend4RealTransformHandler;

/**
 * Factory for creating transform handlers based on the current viewport context.
 * Determines the appropriate handler type based on which viewport has focus.
 */
class FTransformHandlerFactory
{
public:
	/**
	 * Create the appropriate transform handler based on the current viewport context.
	 * @return A new handler instance, or nullptr if the current context is not supported.
	 */
	static TSharedPtr<IBlend4RealTransformHandler> CreateHandler();
};
