#pragma once
#include "dirent.h"
#include "ZenCommon.h"

// Max number of child nodes
#define MAX_CHILD_NODES_COUNT 100

// Max number of all nodes
#define MAX_NODES_COUNT 1000

//Length of project Id
#define PROJECT_ID_LENGTH 37

//Max string length of false and true childs
#define FIRST_LEVEL_RELATIONS_STRING_LENGTH 512

//Function declarations
typedef int(*ptrOnImplementationInit)(char*);
typedef Node**(*ptrOnGetNodesToExecute)(Node*, int*);
typedef int(*ptrOnNodeInit)(Node*);
typedef int(*ptrOnNodePreInit)(Node*);
typedef int(*ptrExecuteAction)(Node*);
typedef int(*ptrSubscribeNodeToEvent)(Node*);
typedef int(*ptrOnNodeComplete)(Node*);

int execNode(Node *node);
void StartNodeCore(Node* node, int* isNodeFirstFires);
void RunNodeInterfaces(Node* node);
void StartNode(void *context);
void StartLoops();
void StartOrSignalNodes(Node** nodes, int startNodesCnt, Node **stopNodeList, int *iStopNodesListCnt);
void AddStopParentsToList(Node **nodes, int stopNodesCnt, Node **stopNodeList, int *iStopNodesListCnt);
void OnNodeFinish(Node* node);
void ReadZenFile(char zenFileName[MAX_PATH], char **input);
void SetProjectId(const char *sDir);
int FillImplementationList();
void FillNodeList();
void FillRelationList();
void SyncChilds(Node *currentNode, int loopLockId);
void SyncLoops();
void SetPaths();
void ExecuteMainThreadActions();
void SyncLoop();
void MakeVirtualconnections();
void SafeNodeStart(Node *node);
void ReadEngineConfiguration();
void DeleteObsoleteNodeFiles();