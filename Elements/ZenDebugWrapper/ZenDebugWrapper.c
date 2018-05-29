#include "ZenDebugWrapper.h"
#include "ZenCommon.h"
#include "ZenCoreCLR.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

EXTERN_DLL_EXPORT int onNodePreInit(Node* node)
{
	coreclr_init_app_domain();
	node->implementationContext = malloc(sizeof(int));
	*((int*)(node->implementationContext)) = coreclr_create_delegates("ZenDebug", 0);
	coreclr_init_managed_nodes(*((int*)(node->implementationContext)), node);
	return 0;
}

EXTERN_DLL_EXPORT int executeAction(Node *node)
{
	char* result = malloc(100 * sizeof(char));
	coreclr_execute_action(*((int*)(node->implementationContext)), node, result);
	node->isConditionMet = 1;
	free(result);
	return 0;
}