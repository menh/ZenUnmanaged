/*************************************************************************
 * Copyright (c) 2015, 2018 Zenodys BV
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Contributors:
 *    Tomaž Vinko
 *   
 **************************************************************************/

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

