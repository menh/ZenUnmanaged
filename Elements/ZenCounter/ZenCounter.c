#include "ZenCounter.h"
#include "ZenCommon.h"
#include <stdlib.h>
#include <stdio.h>

EXTERN_DLL_EXPORT int onNodePreInit(Node* node)
{
	node->lastResult = malloc(1 * sizeof(int*));
	node->lastResult[0] = malloc(sizeof(int));
	*((int*)node->lastResult[0]) = atoi(common_get_node_arg(node, "INITIAL_VALUE"));
	node->lastResultType = RESULT_TYPE_INT;
	return 0;
}

EXTERN_DLL_EXPORT int executeAction(Node *node)
{
	int currentCounterValue = *(int*)node->lastResult[0] + atoi(common_get_node_arg(node, "COUNTER_STEP"));
	node->isConditionMet = currentCounterValue >= atoi(common_get_node_arg(node, "MAX_VALUE"));

	if (currentCounterValue >= atoi(common_get_node_arg(node, "MAX_VALUE")))
		currentCounterValue = atoi(common_get_node_arg(node, "INITIAL_VALUE"));

	*((int*)node->lastResult[0]) = currentCounterValue;

	return 0;
}