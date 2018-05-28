#include "ZenCommon.h"
#include "ZenCoreCLR.h"
#include "ZenElementsExecuterWrapper.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

EXTERN_DLL_EXPORT int onNodePreInit(Node* node)
{
	coreclr_init_app_domain();
	node->implementationContext = malloc(sizeof(int));
	*((int*)(node->implementationContext)) = coreclr_create_delegates("ZenElementsExecuter", 1);
	coreclr_init_managed_nodes(*((int*)(node->implementationContext)), node);
	return 0;
}

EXTERN_DLL_EXPORT int executeAction(Node *node)
{
	char* result = malloc(100 * sizeof(char));
	coreclr_execute_action(*((int*)(node->implementationContext)), node, result);
	int numDisconnectedNodes = 0;
	char** disconnectedNodes = NULL;

	common_str_split(result, ",", &numDisconnectedNodes, &disconnectedNodes);

	node->disconnectedNodes = malloc((numDisconnectedNodes - 1) * sizeof(Node*));
	common_parse_nodes(node->disconnectedNodes, disconnectedNodes, numDisconnectedNodes);

	common_free_splitted_string(disconnectedNodes, numDisconnectedNodes);

	node->disconnectedNodesCnt = numDisconnectedNodes - 1;
	node->isConditionMet = 1;
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