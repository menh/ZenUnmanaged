#include "ZenStart.h"
#include "ZenCommon.h"
#include <stdlib.h>

EXTERN_DLL_EXPORT int onImplementationInit(char *params)
{
	return 0;	
}

EXTERN_DLL_EXPORT int executeAction(Node *node)
{
	node->isConditionMet = 1;
	return 0;
}


