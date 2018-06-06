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

#pragma once

void InitNodeDatas();
EXTERN_DLL_EXPORT int coreclr_init_app_domain();
EXTERN_DLL_EXPORT int coreclr_create_delegates(char* fileName, int canContainDynamicNodes);
EXTERN_DLL_EXPORT void coreclr_init_managed_nodes(int pos, Node* node);
EXTERN_DLL_EXPORT void coreclr_execute_action(int pos, Node* node, char* result);
EXTERN_DLL_EXPORT void coreclr_on_node_init(int pos, Node* node, char* result);
EXTERN_DLL_EXPORT void coreclr_get_dynamic_nodes(int pos, Node* node, char **result);
#if defined(__cplusplus) && defined (_WIN32)
DWORD CreateAppDomain(LPCWSTR domainName);
#endif