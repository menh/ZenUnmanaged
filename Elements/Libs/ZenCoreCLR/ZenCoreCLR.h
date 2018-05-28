#pragma once

void InitNodeDatas();
EXTERN_DLL_EXPORT int coreclr_init_app_domain();
EXTERN_DLL_EXPORT int coreclr_create_delegates(char* fileName, int canContainDynamicNodes);
EXTERN_DLL_EXPORT void coreclr_init_managed_nodes(int pos, Node* node);
EXTERN_DLL_EXPORT void coreclr_execute_action(int pos, Node* node, char* result);
EXTERN_DLL_EXPORT void coreclr_on_node_init(int pos, Node* node, char* result);
EXTERN_DLL_EXPORT void coreclr_get_dynamic_nodes(int pos, Node* node, char **result);
#if defined(__cplusplus) && defined (_MSC_VER)
DWORD CreateAppDomain(LPCWSTR domainName);
#endif