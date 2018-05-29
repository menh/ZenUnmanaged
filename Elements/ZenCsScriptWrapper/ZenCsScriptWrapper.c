#include "ZenCommon.h"
#include "ZenCoreCLR.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

EXTERN_DLL_EXPORT int onNodePreInit(Node* node)
{
	coreclr_init_app_domain();
	node->implementationContext = malloc(sizeof(int));
	*((int*)(node->implementationContext)) = coreclr_create_delegates("ZenCsScript", 1);
	coreclr_init_managed_nodes(*((int*)(node->implementationContext)), node);
	return 0;
}

EXTERN_DLL_EXPORT int onNodeInit(Node* node)
{
	char* result = malloc(100 * sizeof(char));
	coreclr_on_node_init(*((int*)(node->implementationContext)), node, result);
	free(result);
	return 0;
}

EXTERN_DLL_EXPORT int executeAction(Node *node)
{
	char* result = malloc(100 * sizeof(char));
	node->isConditionMet = 1;
	coreclr_execute_action(*((int*)(node->implementationContext)), node, result);
	free(result);
	return 0;
}

EXTERN_DLL_EXPORT Node** getNodesToExecute(Node* node, int* nodesToExecuteCnt)
{
	char* result = NULL;
	coreclr_get_dynamic_nodes(*((int*)(node->implementationContext)), node, &result);

	int numNodesToExecute = 0;
	char **nodesToExecute = NULL;
	common_str_split(result, ",", &numNodesToExecute, &nodesToExecute);

	Node** disconnectedNodes = malloc((numNodesToExecute) * sizeof(Node*));

	common_parse_nodes(disconnectedNodes, nodesToExecute, numNodesToExecute - 1);
	*nodesToExecuteCnt = numNodesToExecute - 1;
	common_free_splitted_string(nodesToExecute, numNodesToExecute);
	return disconnectedNodes;
}