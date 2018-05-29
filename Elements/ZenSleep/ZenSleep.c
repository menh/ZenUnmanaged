#include "ZenSleep.h"
#include "ZenCommon.h"
#include <stdlib.h>
#include <stdio.h>

#ifdef __linux__
#include <unistd.h>
#else
#include <windows.h>
#endif

EXTERN_DLL_EXPORT int onImplementationInit(char *params)
{
	return 0;
}

EXTERN_DLL_EXPORT int executeAction(Node *node)
{
	int i = atoi(common_get_node_arg(node, "SLEEP_TIME"));
#ifdef __linux__
	usleep(i * 1000);
#else
	Sleep(i);
#endif

	node->isConditionMet = 1;
	return 0;
}

