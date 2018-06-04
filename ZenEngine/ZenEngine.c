/*==============================================================================================================================
|
|       Main zeno engine workflow logic
|
+-------------------------------------------------------------------------------------------------------------------------------
|
|   Known Bugs:		* none
|
|	     To Do:		* Do not allocate memory instead of allocating max, for event buffers length of zero
|					* Create on node complete callback:
|						ptrOnNodeComplete onNodeCompleteFunct = 
							(ptrOnNodeComplete)GetFunction(((Node**)(node->nodesToTrigger))[i]->implementation, "onNodeComplete");
|						
					  if (onNodeCompleteFunct)
|							onNodeCompleteFunct(((Node**)(node->nodesToTrigger))[i]);
*================================================================================================================================*/
#include "ZenEngine.h"
#include <errno.h>
#include <time.h>
#include "pthread.h"
#include <stdio.h>
#include <stdlib.h>
#include "ZenCommon.h"
#include "os_call.h"
#include "cJSON.h"
#include <string.h>
#include "ini.h"

const char* ENGINE_VERSION = "1.0.0";

const char* PROP_ELEMENT_NAME = "ELEMENT_NAME";
const char* PROP_IMPLEMENTATION = "IMPLEMENTATION";
const char* PROP_OPERATOR = "OPERATOR";

const char* ELEMENT_TYPE_ACTION = "ACTION";
const char* ELEMENT_TYPE_EVENT = "EVENT";

const char* STATUS_RUNNING = "RUNNING";
const char* STATUS_ARRIVED = "ARRIVED";
const char* STATUS_STOPPED = "STOPPED";

const int MAX_EVENT_QUEUE_LENGTH = 100000;

struct Implementation **_implementationList;
char _projectId[PROJECT_ID_LENGTH];


// Loop, node and start node locks
pthread_mutex_t _loop_locks[1000];
int _loop_locks_cnt = 0;

pthread_mutex_t _node_locks[1000];
int _node_locks_cnt = 0;

pthread_mutex_t node_start_mutex = PTHREAD_MUTEX_INITIALIZER;

//Sync helpers
char** _currentExecutorNodes;
int _currentExecutorNodesCnt = 0;

//Paths
char _working_directory[MAX_PATH];
char _project_root[MAX_PATH];
char _implementations_file[MAX_PATH];
char _settings_file[MAX_PATH];
char _relations_file[MAX_PATH];
char _nodes_file[MAX_PATH];
char _implementations_path[MAX_PATH];

int _implementationCount;

struct nodeContextParamsStruct {
	int async;
	Node* node;
};

struct nodeContextParamsStruct node_context[MAX_NODES_COUNT];
pthread_t node_threads[MAX_NODES_COUNT];
int nodeContextParamsCnt = 0;

pthread_mutex_t pause_node_mutex = PTHREAD_MUTEX_INITIALIZER;

//************************************************************************/
//************************ START MAIN ************************************/
//************************************************************************/
int main(int argc, char **argv)
{
	char input[10];
	
	strncpy(engineConfiguration.engineVersion, ENGINE_VERSION, strlen(ENGINE_VERSION) + 1);
	printf("ZenoEngine v%s started.....\n",ENGINE_VERSION);
	
	SetPaths();
	ReadEngineConfiguration();
	common_init_project(_project_root, _projectId, engineConfiguration, execNode);

	strncpy(engineConfiguration.workingDir, _working_directory, strlen(_working_directory) + 1);
	strncpy(engineConfiguration.projectRoot, _project_root, strlen(_project_root) + 1);
	
	DeleteObsoleteNodeFiles();
	printf("Instance : %s\n\n\n", engineConfiguration.workstationName);
	ConnectMqtt(engineConfiguration);
	FillImplementationList();
	FillNodeList();
	ExecuteMainThreadActions();
	FillRelationList();
	SyncLoops();
	StartLoops();
	fgets(input, BUFSIZ, stdin);

	while (strcmp(input, "quit\n") != 0) {
		fgets(input, BUFSIZ, stdin);
	}
	return 0;
}
//************************************************************************/
//************************ END MAIN **************************************/
//************************************************************************/


//************************************************************************/
//************************ START STARTING NODES **************************/
//************************************************************************/

/**
* Callback for node execution request. It can be triggered from managed or unmanaged code
*
* @param	node	node which is going to be executed
* @return	int
*/
int execNode(Node *node)
{
	struct nodeContextParamsStruct nodeCtx;
	nodeCtx.async = 0;
	nodeCtx.node = node;
	StartNode(&nodeCtx);
	return 0;
}

/**
* Safely starts node main loop. Node can be started as start node, or node that starts after parent node ends.
* node_context collection must be thread safe.
*
* @param node	node which StartNode function is going to be called in separate thread
* @return	void
*/
void SafeNodeStart(Node *node)
{
	pthread_mutex_lock(&node_start_mutex);
	node_context[nodeContextParamsCnt].async = 1;
	node_context[nodeContextParamsCnt].node = node;
	pthread_create(&node_threads[nodeContextParamsCnt], NULL, StartNode, &node_context[nodeContextParamsCnt]);
	nodeContextParamsCnt++;
	pthread_mutex_unlock(&node_start_mutex);
}

/**
* Execute "Start" nodes, each one in new thread.
* This is the first function in "starting nodes" series.
* Nodes are started with async parameter setted to true. This means that for each node new thread with endless loop is created
* @return	void
*/
void StartLoops()
{
	int i;
	for (i = 0; i < COMMON_NODE_LIST_LENGTH; i++)
	{
		if ((strcmp(COMMON_NODE_LIST[i]->implementationId, "ZenStart#0#") == 0) && strcmp(common_get_node_arg(COMMON_NODE_LIST[i], "ACTIVE"), "0") != 0)
			SafeNodeStart(COMMON_NODE_LIST[i]);
	}
}

/**
* Starts main node endless loop. It locks the whole node loop, because:
*		OnNodeFinish starts child nodes. Problem is that child nodes can be faster that parent nodes (ones that startes them).
*		In this case, child node can signal parent thread, before they even went into pause state.
*	    Signalling is lost so parents will never be signalled.
*
* @param context	node context (node struct & async flag)
* @return			void
*/
void StartNode(void *context)
{
	struct nodeContextParamsStruct *nodeParams = context;
	nodeParams->node->isStarted = 1;
	int isNodeFirstFire = 1;

	if (nodeParams->async)
	{
		while (1)
		{
			if (common_is_debug_mode_enabled())
				common_wait_debug_signal();

			// Lock main loop sync
			pthread_mutex_lock(&_loop_locks[nodeParams->node->loopLockId]);

			StartNodeCore(nodeParams->node, &isNodeFirstFire);

			// Mutex is automatically released
			common_wait_pause_condition(nodeParams->node, &_loop_locks[nodeParams->node->loopLockId]);
			// Release mutex, because is automatically obtained again after signaling
			pthread_mutex_unlock(&_loop_locks[nodeParams->node->loopLockId]);
		}
	}
	else
		StartNodeCore(nodeParams->node, &isNodeFirstFire);
}

/**
* Main start node logic. It can be called from engine's StartNode loop or from nodes
*
* @param	node	node to start
* @return	void
*/
void StartNodeCore(Node* node, int* isNodeFirstFire)
{
	// Calls onNodeInit and executeAction functions
	RunNodeInterfaces(node);

	// Don't fire finish event for eventable nodes without paused thread (on first loop time).
	// In this step just pause the thread, and wait for event to arrive.
	// OnNodeFinish starts child nodes
	if (node->isActionable || !isNodeFirstFire)
		OnNodeFinish(node);
	else
		*(isNodeFirstFire) = 0;
}

/**
* Runs node interface functions:
*		+) onNodeInit
*		+) executeAction
*
* @param	context		node context (node struct & async flag)
* @return	void
*/
void RunNodeInterfaces(Node* node)
{
	node->started = 1;
	node->isEventActive = 1;
	strncpy(node->status, STATUS_RUNNING, strlen(STATUS_RUNNING) + 1);
	strncpy(node->errorMessage, "", 1);
	node->errorCode = 0;

	ptrOnNodeInit onNodeInitFunct = (ptrOnNodeInit)GetFunction(node->implementation, "onNodeInit");
	if (onNodeInitFunct && !node->isInitialized)
	{
		onNodeInitFunct(node);
		node->isInitialized = 1;
	}

	ptrExecuteAction executeActionFunct = (ptrExecuteAction)GetFunction(node->implementation, "executeAction");
	if (executeActionFunct)
	{
		node->started = time(NULL);
		executeActionFunct(node);
	}
}

/**
* Called after entry points (start nodes) are executed
* Each node can be in one of two states:
*		+) if has already been executed, then it's in paused state. In this case, just signal the node thread.
*		+) if hasn't been executed, then start it in new thread (simillar like entry points are started in StartLoops function)
*
* Here, also list of nodes that are going to be stopped is filled:
*		+) Go through each true and false parent of the node that is going to be started
*		+) Check if it's in STOPPED state, and doesn't exists already in stopped nodes list
*		+) If true, then add it to the list
*
* @param	nodes				nodes that are going to be started
* @param	startNodesCnt		how many nodes are going to be started
* @param	stopNodeList		list of nodes that are going to be stopped. This is output argument.
* @param	iStopNodesListCnt	how many nodes are going to be stopped. This is output argument.
* @return	void
*/
void StartOrSignalNodes(Node** nodes, int startNodesCnt, Node **stopNodeList, int *iStopNodesListCnt)
{
	int i;
	for (i = 0; i < startNodesCnt; i++)
	{
		pthread_mutex_lock(&_node_locks[nodes[i]->nodeLockId]);
		while (strcmp(nodes[i]->status, "RUNNING") == 0)
		{
			printf("Node %s is running....\n", nodes[i]->id);
			//Sleep(10);
		}

		AddStopParentsToList(nodes[i]->ptrTrueParents, nodes[i]->trueParentsCnt, stopNodeList, iStopNodesListCnt);
		AddStopParentsToList(nodes[i]->ptrFalseParents, nodes[i]->falseParentsCnt, stopNodeList, iStopNodesListCnt);
		if (!nodes[i]->isStarted)
			SafeNodeStart(nodes[i]);
		else
			common_signal_pause_condition(nodes[i]->pauseNodeConditionId);

		pthread_mutex_unlock(&_node_locks[nodes[i]->nodeLockId]);
	}
}
//************************************************************************/
//************************ END STARTING NODES ****************************/
//************************************************************************/


//************************************************************************/
//************************ START HELPERS *********************************/
//************************************************************************/

/**
* Last chance to execute code on main thread. All code exeuted here is thread safe. Node list here is fully filled.
* Suitable for:
*		+) Executing onNodePreInit event in nodes.
*		+) Filling event buffers
*
* @return	void
*/
void ExecuteMainThreadActions()
{
	int i, j;
	for (i = 0; i < COMMON_NODE_LIST_LENGTH; i++)
	{
		// Fire onNodePreInit event
		ptrOnNodePreInit onNodePreInit = (ptrOnNodePreInit)GetFunction(COMMON_NODE_LIST[i]->implementation, "onNodePreInit");
		if (NULL != onNodePreInit)
			onNodePreInit(COMMON_NODE_LIST[i]);

		// Fill buffer triggers
		if (strcmp(common_get_node_arg(COMMON_NODE_LIST[i], "__BUFFER_TRIGGERS__"), "") != 0)
		{
			// Current node is triggering node
			Node* triggeringNode = COMMON_NODE_LIST[i];
			// Trigger nodes are property of triggering node in string form : TriggerNode1,TriggerNode2....
			int numBufferTriggers = 0;
			char **bufferTriggers = NULL;
			common_str_split(common_get_node_arg(COMMON_NODE_LIST[i], "__BUFFER_TRIGGERS__"), ",", &numBufferTriggers, &bufferTriggers);

			if (strcmp(common_get_node_arg(COMMON_NODE_LIST[i], "__EVENTS_BUFFER_LENGTH__"), "") == 0)
				common_init_buffer(&COMMON_NODE_LIST[i]->bufferedEvents, MAX_EVENT_QUEUE_LENGTH);
			else
				common_init_buffer(&COMMON_NODE_LIST[i]->bufferedEvents, atoi(common_get_node_arg(COMMON_NODE_LIST[i], "__EVENTS_BUFFER_LENGTH__")));

			// Go through splitted trigger nodes
			for (j = 0; j < numBufferTriggers; j++)
			{
				// Find trigger node (eg Debug1)
				Node* triggerNode = common_get_node_by_id(bufferTriggers[j]);
				if (triggeringNode->nodesToTrigger == NULL)
					triggerNode->nodesToTrigger = malloc(100 * sizeof(Node*));

				// Add triggering node to trigger node (eg OpcUaClientSubs to Debug1)
				triggerNode->nodesToTrigger[triggerNode->nodesToTriggerCnt] = (Node*)malloc(sizeof(Node*));
				triggerNode->nodesToTrigger[triggerNode->nodesToTriggerCnt++] = triggeringNode;

				printf("%s added to %s triggering nodes list...\n", triggeringNode->id, triggerNode->id);
			}
			common_free_splitted_string(bufferTriggers, numBufferTriggers);
		}
	}
}

/**
* Fills list of nodes, that are going to be stopped.
* Add it to the list if it's in STOPPED state, and doesn't exists already in stopped nodes list
*
* @param	nodes				true or false parents list (depending of call from calee)
* @param	stopNodesCnt		how many nodes are on true or false parents list
* @param	stopNodeList		list of nodes that are going to be stopped. This is output argument.
* @param	iStopNodesListCnt	how many nodes are going to be stopped. This is output argument.
* @return	void
*/
void AddStopParentsToList(Node **nodes, int stopNodesCnt, Node **stopNodeList, int *iStopNodesListCnt)
{
	int i;
	if (nodes == NULL)
		return;

	for (i = 0; i < stopNodesCnt; i++)
	{
		if (strcmp(((Node*)nodes[i])->status, "STOPPED") != 0 && !common_node_exists(stopNodeList, ((Node*)nodes[i]), (*iStopNodesListCnt)))
			stopNodeList[(*iStopNodesListCnt)++] = nodes[i];
	}
}

/**
* Generic file reader (implementations, relations....)
*
* @param	zenFileName		file name to be read
* @param	input			content of readed file (output argument)
* @return	void
*/
void ReadZenFile(char zenFileName[MAX_PATH], char **input)
{
	FILE *fp;
	long lSize;

	fp = fopen(zenFileName, "rb");
	if (!fp) perror("blah.txt"), exit(1);

	fseek(fp, 0L, SEEK_END);
	lSize = ftell(fp);
	rewind(fp);

	/* allocate memory for entire content */
	*input = calloc(1, lSize + 1);
	if (!*input) fclose(fp), fputs("memory alloc fails", stderr), exit(1);

	/* copy the file into the buffer */
	if (1 != fread(*input, lSize, 1, fp))
		fclose(fp), free(*input), fputs("entire read fails", stderr), exit(1);

	fclose(fp);
}

/**
* Searches for first folder under project root. Folder name is project id
* Only one folder, with project id as name, can be under project root!!!
*
* @param	sDir	project folder root
* @return	void
*/
void SetProjectId(const char *sDir)
{
	struct dirent* dent;
	DIR* srcdir = opendir(sDir);

	if (srcdir == NULL)
	{
		printf("Cannot find %s\n", sDir);
		return;
	}

	while ((dent = readdir(srcdir)) != NULL)
	{
		if (strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0)
			continue;

		strncpy(_projectId, dent->d_name, strlen(dent->d_name) + 1);
		break;
	}
	closedir(srcdir);
}

/**
* Checks if zenodys.zip file exists. If file is found, then delete all other content (old project dirs)
* and extract new project dir from zendys.zip file. At the end delete also zenodys.zip, because we don't want to
* repeate procedure on next reboot
*
* @return	void
*/
void HandleZip(char project_root[MAX_PATH])
{
	int rc;
	char zipFile[MAX_PATH];
	char dirToRemove[MAX_PATH];
	FILE * file;

	snprintf(zipFile, sizeof(zipFile), "%s%s", project_root, "/zenodys.zip");

	file = fopen(zipFile, "r");

	// zenodys.zip file exists
	if (file)
	{
		struct dirent* dent;
		DIR* srcdir = opendir(project_root);

		if (srcdir == NULL)
		{
			printf("Cannot find %s\n", project_root);
			exit(1);
		}

		//Remove all folders and files, except zenodys.zip
		while ((dent = readdir(srcdir)) != NULL)
		{
			if (strcmp(dent->d_name, "zenodys.zip") == 0 || strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0)
				continue;
			
			snprintf(dirToRemove, sizeof(dirToRemove), "%s%s%s", project_root, "/", dent->d_name);

			//Remove non empty directory...
			rc = common_remove_directory(dirToRemove);
			if (rc != 0)
			{
				printf("Fatal error : Cannot remove directory %s\n", dent->d_name);
				exit(1);
			}
		}
		closedir(srcdir);

		//Extract zenodys.zip content
		rc = common_zip_extract(zipFile, project_root, 'b');
		if (rc != 0)
		{
			printf("Fatal error : problem with extracting zenodys.zip archive.");
			exit(1);
		}
		fclose(file);
		//Delete zenodys.zip file
		rc = remove(zipFile);
	}
}

/**
* Sets paths to db files, based on current directory
*
* @return	void
*/
void SetPaths()
{
	char project_root[256];

	//Gets current directory
	if (getcwd(_working_directory, sizeof(_working_directory)) == NULL)
	{
		fprintf(stderr, "Failed to retrieve current directory...");
		getchar();
		exit(1);
	}
	
	//Sets project root
	snprintf(project_root, sizeof(project_root), "%s%s", _working_directory, "/project");

	//Extract zenodys.zip project, if exists.
	HandleZip(project_root);

	SetProjectId(project_root);
	
	snprintf(_project_root, sizeof(_project_root), "%s%s%s", project_root, "/", _projectId);

	//Sets Settings.ini path
	snprintf(_settings_file, sizeof(_settings_file), "%s%s", _project_root, "/Settings.ini");

	//Sets implementations, relations and nodes file names
	snprintf(_implementations_file, sizeof(_implementations_file), "%s%s", _project_root, "/DB/Implementations.zen");
	snprintf(_relations_file, sizeof(_relations_file), "%s%s", _project_root, "/DB/Relations.zen");
	snprintf(_nodes_file, sizeof(_nodes_file), "%s%s", _project_root, "/DB/Modules.zen");
	snprintf(_implementations_path, sizeof(_implementations_path), "%s%s", _project_root, "/Implementations/");
}
//************************************************************************/
//************************ END HELPERS ***********************************/
//************************************************************************/

//************************************************************************/
//************************ START FILLS ***********************************/
//************************************************************************/

/**
* Fills implementations from implementation file
*
* @return	void
*/
int FillImplementationList()
{
	char *input = 0;
	int i;

	ReadZenFile(_implementations_file, &input);
	int numImplementations = 0;

	//Split implementations
	char **implementations = NULL;
	common_str_split(input, ";", &numImplementations, &implementations);

	for (i = 0; i < numImplementations; i++)
	{
		if (strcmp(implementations[i], "") == 0)
			break;

		if (_implementationList == NULL)
			_implementationList = malloc(sizeof(struct Implementation*));
		else
		{
			struct Implementation **tmpImp = realloc(_implementationList, (_implementationCount + 1) * sizeof *_implementationList);
			if (tmpImp == NULL)
			{
				fprintf(stderr, "could not allocate implementation...");
				getchar();
				exit(1);
			}
			else
				_implementationList = tmpImp;
		}

		//Get basic implementation data
		char tmpImpFile[MAX_PATH] = "";
		struct Implementation *implementation = malloc(sizeof(struct Implementation));
		int numImplementation = 0;
		char** tokens = NULL;
		common_str_split(implementations[i], ",", &numImplementation, &tokens);
		sscanf(tokens[0], "%s", implementation->fileName);
		sscanf(tokens[1], "%s", implementation->id);
		sscanf(tokens[3], "%s", implementation->params);
		common_free_splitted_string(tokens, numImplementation);

		//Load implementation shared library
		snprintf(tmpImpFile, sizeof(tmpImpFile), "%s%s", _implementations_path, implementation->fileName);

		void *hDLL = LoadSharedLibrary(tmpImpFile);
		if (hDLL == 0)
		{
			fprintf(stderr, "Could not load implementation %s...", tmpImpFile);
			getchar();
			exit(1);
		}
		implementation->hDLL = hDLL;

		// Check if executeAction exists
		//	* if does, then this is actionable node
		//	* else, it's eventable
		if ((ptrExecuteAction)GetFunction(hDLL, "executeAction"))
			strncpy(implementation->type, ELEMENT_TYPE_ACTION, strlen(ELEMENT_TYPE_ACTION) + 1);
		else
			strncpy(implementation->type, ELEMENT_TYPE_EVENT, strlen(ELEMENT_TYPE_EVENT) + 1);

		//Call OnImplementationInit function. This function is called on main thread, and it's thread safe
		ptrOnImplementationInit onImplementationInitFunct = (ptrOnImplementationInit)GetFunction(hDLL, "onImplementationInit");
		if (onImplementationInitFunct)
			onImplementationInitFunct(implementation->params);

		//Add implementation to implementation list, and reallocate list properly
		_implementationList[i] = implementation;
		_implementationCount++;
	}
	//for (int i = 0; i < numImplementations; i++)
	//	free(implementations[i]);

	common_free_splitted_string(implementations, numImplementations);
	free(input);
	return 0;
}

/**
* Fills nodes from node file
*
* @return	void
*/
void FillNodeList()
{
	char *input = 0;
	int i, j;

	ReadZenFile(_nodes_file, &input);
	cJSON *root = cJSON_Parse(input);

	int nodesCount = cJSON_GetArraySize(root);
	common_initialize_node_list(nodesCount);
	for (i = 0; i < nodesCount; i++)
	{
		cJSON *subitem = cJSON_GetArrayItem(root, i);
		Node *node = malloc(sizeof(Node));

		cJSON* tmpNodeProperties = cJSON_GetObjectItem(subitem, "ELEMENT_PROPERTIES");
		cJSON *nodeProperties = tmpNodeProperties->child;
		node->argsCnt = cJSON_GetArraySize(tmpNodeProperties);
		node->args = malloc(node->argsCnt * sizeof(nodeArgs*));
		int iCnt = 0;
		while (nodeProperties)
		{
			node->args[iCnt] = malloc(sizeof(nodeArgs));
			node->args[iCnt]->Key = malloc(strlen(nodeProperties->string) * sizeof(char) + 1);
			node->args[iCnt]->Value = malloc(strlen(nodeProperties->valuestring) * sizeof(char) + 1);
			
			strncpy(node->args[iCnt]->Key, nodeProperties->string, strlen(nodeProperties->string) + 1);
			strncpy(node->args[iCnt]->Value, nodeProperties->valuestring, strlen(nodeProperties->valuestring) + 1);
			
			nodeProperties = nodeProperties->next;
			iCnt++;
		}

		strncpy(node->id, cJSON_GetObjectItem(subitem, "ELEMENT_NAME")->valuestring, strlen(cJSON_GetObjectItem(subitem, "ELEMENT_NAME")->valuestring) + 1);
		strncpy(node->implementationId, cJSON_GetObjectItem(subitem, "IMPLEMENTATION")->valuestring, strlen(cJSON_GetObjectItem(subitem, "IMPLEMENTATION")->valuestring) + 1);
		strncpy(node->nodeOperator, cJSON_GetObjectItem(subitem, "OPERATOR")->valuestring, strlen(cJSON_GetObjectItem(subitem, "OPERATOR")->valuestring) + 1);

		node->isInitialized = 0;
		node->isStarted = 0;
		node->isConditionMet = 1;
		node->loopLockId = -1;
		node->pauseNodeConditionId = -1;
		node->ptrTrueParents = NULL;
		node->ptrFalseParents = NULL;
		node->falseChildsCnt = 0;
		node->falseParentsCnt = 0;
		node->trueChildsCnt = 0;
		node->trueParentsCnt = 0;
		node->unregisterEvent = 0;
		node->disconnectedNodes = NULL;
		node->disconnectedNodesCnt = 0;
		node->nodesToTrigger = NULL;
		node->nodesToTriggerCnt = 0;
		node->isEventActive = 0;
		node->hasGreenLight = 1;
		strncpy(node->status, "", 1);

		for (j = 0; j < _implementationCount; j++)
		{
			if (strcmp(node->implementationId, _implementationList[j]->id) == 0)
			{
				node->implementation = _implementationList[j]->hDLL;
				if (strcmp(_implementationList[j]->type, "ACTION") == 0)
					node->isActionable = 1;
				else
				{
					node->isActionable = 0;
					common_init_event_queue_lock(node);
				}

				ptrSubscribeNodeToEvent subscribeNodeToEvent = (ptrSubscribeNodeToEvent)GetFunction(_implementationList[j]->hDLL, "onSubscribeNodeToEvent");
				if (NULL != subscribeNodeToEvent)
				{
					int tmp = subscribeNodeToEvent(node);
				}
			}
		}

		pthread_mutex_init(&_node_locks[_node_locks_cnt++], NULL);
		node->nodeLockId = _node_locks_cnt - 1;

		common_add_node_to_list(node);
	}
	cJSON_Delete(root);
	free(input);
}

/**
* Fills relations from relation file and manages first level parent / child relations
*
* @return	void
*/
void FillRelationList()
{
	//id,true_child_1|true_child_2,false_child_1
	char *input = 0;
	int numRelations = 0, i, j, k, iRel;

	ReadZenFile(_relations_file, &input);

	printf("------------DEFINING RELATIONS-------------\n");
	//split relations, handle them one by one
	char **relations = NULL;
	common_str_split(input, ";", &numRelations, &relations);
	for (i = 0; i < numRelations; i++)
	{
		int numRelation = 0;
		char **relation = NULL;
		common_str_split(relations[i], ",", &numRelation, &relation);
		if (strcmp(*relation, "") == 0)
		{
			common_free_splitted_string(relation, numRelation);
			break;
		}
		char id[50], trueChilds[FIRST_LEVEL_RELATIONS_STRING_LENGTH] = "", falseChilds[FIRST_LEVEL_RELATIONS_STRING_LENGTH] = "";
		//Node Id
		sscanf(relation[0], "%s", id);

		//True childs, separated by |
		if (relation[1] != NULL)
			sscanf(relation[1], "%s", trueChilds);

		//False childs, separated by |
		if (relation[2] != NULL)
			sscanf(relation[2], "%s", falseChilds);

		for (j = 0; j < COMMON_NODE_LIST_LENGTH; j++)
		{
			//Find node that matches id field from relation string
			if (strcmp(COMMON_NODE_LIST[j]->id, id) == 0)
			{
				//Split true childs by | separator
				int numTrueChilds = 0;
				char** ptrTrueChilds = NULL;
				common_str_split(trueChilds, "|", &numTrueChilds, &ptrTrueChilds);

				//Split false childs by | separator
				int numFalseChilds = 0;
				char** ptrFalseChilds = NULL;
				common_str_split(falseChilds, "|", &numFalseChilds, &ptrFalseChilds);

				if (numTrueChilds > 0 && strcmp(*ptrTrueChilds, "") != 0)
				{
					//Set true childs node array
					Node **tmpArrTrueChilds = malloc((numTrueChilds) * sizeof(Node*));
					common_parse_nodes(tmpArrTrueChilds, ptrTrueChilds, numTrueChilds);
					COMMON_NODE_LIST[j]->ptrTrueChilds = tmpArrTrueChilds;
					COMMON_NODE_LIST[j]->trueChildsCnt = numTrueChilds;

					//Print relations from current node to childs
					for (iRel = 0; iRel < numTrueChilds; iRel++)
						printf("%s --> %s\n", id, COMMON_NODE_LIST[j]->ptrTrueChilds[iRel]->id);

					//Add contra relation. Current node is true parent to true childs
					for (k = 0; k < numTrueChilds; k++)
					{
						if (tmpArrTrueChilds[k]->ptrTrueParents == NULL)
							tmpArrTrueChilds[k]->ptrTrueParents = malloc(sizeof(Node*));
						else
							tmpArrTrueChilds[k]->ptrTrueParents = realloc(tmpArrTrueChilds[k]->ptrTrueParents, (tmpArrTrueChilds[k]->trueParentsCnt + 1) * sizeof(Node*));

						tmpArrTrueChilds[k]->ptrTrueParents[tmpArrTrueChilds[k]->trueParentsCnt++] = COMMON_NODE_LIST[j];

						//Print relation from child to current node (parent)
						printf("%s <-- %s\n", id, tmpArrTrueChilds[k]->id);
					}
				}

				if (numFalseChilds > 0 && strcmp(*ptrFalseChilds, "") != 0)
				{
					Node **tmpArrFalseChilds = malloc((numFalseChilds) * sizeof(Node*));
					common_parse_nodes(tmpArrFalseChilds, ptrFalseChilds, numFalseChilds);
					COMMON_NODE_LIST[j]->ptrFalseChilds = tmpArrFalseChilds;
					COMMON_NODE_LIST[j]->falseChildsCnt = numFalseChilds;

					//Print relations from current node to childs
					for (iRel = 0; iRel < numFalseChilds; iRel++)
						printf("%s --> %s\n", id, COMMON_NODE_LIST[j]->ptrFalseChilds[iRel]->id);

					//Add contra relation
					for (k = 0; k < numFalseChilds; k++)
					{
						if (tmpArrFalseChilds[k]->ptrFalseParents == NULL)
							tmpArrFalseChilds[k]->ptrFalseParents = (Node**)malloc(sizeof(Node*));
						else
							tmpArrFalseChilds[k]->ptrFalseParents = realloc(tmpArrFalseChilds[k]->ptrFalseParents, (tmpArrFalseChilds[k]->falseParentsCnt + 1) * sizeof(Node*));

						tmpArrFalseChilds[k]->ptrFalseParents[tmpArrFalseChilds[k]->falseParentsCnt++] = COMMON_NODE_LIST[j];

						//Print relation from child to current node (parent)
						printf("%s <-- %s\n", id, tmpArrFalseChilds[k]->id);
					}
				}
				common_free_splitted_string(ptrTrueChilds, numTrueChilds);
				common_free_splitted_string(ptrFalseChilds, numFalseChilds);
			}
		}

		common_free_splitted_string(relation, numRelation);
	}
	common_free_splitted_string(relations, numRelations);
	free(input);
	printf("---------END DEFINING RELATIONS-------------\n");
	printf("\n");

}

//************************************************************************/
//************************ END FILLS *************************************/
//************************************************************************/


//**************************************************************************/
//************************ START SYNCING ***********************************/
//**************************************************************************/

/**
* Loops syncing is two phase process :
*	* We need to virtual connect node executers with rest of the nodes inside loop.
*	* We need to recursively go through each node and sync them with rest of nodes inside same loop
*	  Node executers are special cases, as they might execute nodes from other loops. In that case, loops are megred into same sync
*
* @return	none
*/
void SyncLoops()
{
	int i;

	MakeVirtualconnections();
	SyncLoop();

	// Init borrowed disconnected nodes from MakeVirtualconnections
	for (i = 0; i < COMMON_NODE_LIST_LENGTH; i++)
	{
		COMMON_NODE_LIST[i]->disconnectedNodes = NULL;
		COMMON_NODE_LIST[i]->disconnectedNodesCnt = 0;
	}

	//for (int i = 0; i < COMMON_NODE_LIST_LENGTH; i++)
	//	printf("%d, %s \n", _loop_locks[COMMON_NODE_LIST[i]->loopLockId], COMMON_NODE_LIST[i]->id);
}

/**
* Find all node executers, and virtual connect them with rest of the loop.
* This is first of two steps that synced the nodes.
* In second step we can do normal recursive sync, thanks to this virtual connections.
*
* @return	none
*/
void MakeVirtualconnections()
{
	int i;
	for (i = 0; i < COMMON_NODE_LIST_LENGTH; i++)
	{
		// This is node that can dynamically execute nodes
		ptrOnGetNodesToExecute onGetNodesToExecuteFunct = (ptrOnGetNodesToExecute)GetFunction(COMMON_NODE_LIST[i]->implementation, "getNodesToExecute");
		if (onGetNodesToExecuteFunct)
		{
			// Get all nodes that current executor can execute
			int nodesToExecuteCnt = 0;
			// Borrow disconnectedNodes filed.
			COMMON_NODE_LIST[i]->disconnectedNodes = onGetNodesToExecuteFunct(COMMON_NODE_LIST[i], &nodesToExecuteCnt);
			COMMON_NODE_LIST[i]->disconnectedNodesCnt = nodesToExecuteCnt;
		}
	}
}

/**
* Loop sync starting with "Start" node and then recursively sync all nodes inside loop.
* If syncing loop contains node executers that execute nodes from other loops, then this loops are also synced in same step.
*
* @return	none
*/
void SyncLoop()
{
	int i, j;
	for (i = 0; i < COMMON_NODE_LIST_LENGTH; i++)
	{
		// Initialize pause condition. Signalling must be unique per node thread
		COMMON_NODE_LIST[i]->pauseNodeConditionId = common_init_pause_condition();

		// Entry sync point are "Start" nodes
		if (strcmp(COMMON_NODE_LIST[i]->implementationId, "ZenStart#0#") == 0)
		{
			// Do not sync loop if:
			//	* "Start" node has ACTIVE flag setted to 0
			//	* If loop has already been synced. This can occur when node executer from another loop executes node from this loop
			//	  If another loop has already been synced, then this loop was also
			if ((strcmp(common_get_node_arg(COMMON_NODE_LIST[i], "ACTIVE"), "0") == 0) || COMMON_NODE_LIST[i]->loopLockId > -1)
				continue;

			// Initialize loop lock. It must be unique per loop
			pthread_mutex_init(&_loop_locks[_loop_locks_cnt++], NULL);
			COMMON_NODE_LIST[i]->loopLockId = _loop_locks_cnt - 1;

			// Sync all remaining nodes inside same loop
			for (j = 0; j < COMMON_NODE_LIST[i]->trueChildsCnt; j++)
				SyncChilds(COMMON_NODE_LIST[i]->ptrTrueChilds[j], _loop_locks_cnt - 1);

			for (j = 0; j < COMMON_NODE_LIST[i]->falseChildsCnt; j++)
				SyncChilds(COMMON_NODE_LIST[i]->ptrFalseChilds[j], _loop_locks_cnt - 1);
			_loop_locks_cnt++;
		}
	}
}

/**
* Recursively loop all parents and childs from current node.
*
* @param currentNode	current node
* @param loopLockId		loop locks global array index
* @return	none
*/
void SyncChilds(Node *currentNode, int loopLockId)
{
	int i;

	if (currentNode->loopLockId > -1)
		return;

	currentNode->loopLockId = loopLockId;

	for (i = 0; i < currentNode->trueChildsCnt; i++)
		SyncChilds(currentNode->ptrTrueChilds[i], loopLockId);

	for (i = 0; i < currentNode->falseChildsCnt; i++)
		SyncChilds(currentNode->ptrFalseChilds[i], loopLockId);

	for (i = 0; i < currentNode->trueParentsCnt; i++)
		SyncChilds(currentNode->ptrTrueParents[i], loopLockId);

	for (i = 0; i < currentNode->falseParentsCnt; i++)
		SyncChilds(currentNode->ptrFalseParents[i], loopLockId);

	for (i = 0; i < currentNode->disconnectedNodesCnt; i++)
		SyncChilds(currentNode->disconnectedNodes[i], loopLockId);
}
//**************************************************************************/
//************************ END SYNCING *************************************/
//**************************************************************************/


//**************************************************************************/
//************************ START MAIN WF PROCEDURE *************************/
//**************************************************************************/

/**
* Main workflow procedure. Responsible for:
*		1) Adding nodes to start list:
*			+) Current node true and false childs based on their conditions
*			+) Disconnected nodes. They are already evaluated in runtime, inside node executers
*			+) Triggering nodes
*
*		2) Starting or signalling nodes from start list
*		3) Stopping true and false parents from current nodes
*
* @param node	node that raises finish event
* @return		void
*/
void OnNodeFinish(Node* node)
{
	int i, j;
	
	strncpy(node->status, STATUS_ARRIVED, strlen(STATUS_ARRIVED) + 1);
	//printf("Start Doing : %s\n", node->id);

	// Init start node list
	int startNodesCnt = 0;
	Node *startNodes[100];

	// Handle first level true / false node's childs
	// First step : put true / false childs into the same list
	Node *trueAndFalseChilds[100];

	for (i = 0; i < node->trueChildsCnt; i++)
		trueAndFalseChilds[i] = node->ptrTrueChilds[i];

	for (i = 0; i < node->falseChildsCnt; i++)
		trueAndFalseChilds[i + node->trueChildsCnt] = node->ptrFalseChilds[i];

	// Second step : for all nodes from previous list check conditions. If condition satisfies requirements, then put child node to start list
	for (i = 0; i < node->trueChildsCnt + node->falseChildsCnt; i++)
	{
		// Initialize conditions
		int conditions[MAX_CHILD_NODES_COUNT] = { 0 };
		int cntConditions = 0;

		// Loop through current node child's true parents. Get list of their conditions
		// For true parent, condition is met when parent condition is true
		for (j = 0; j < trueAndFalseChilds[i]->trueParentsCnt; j++)
			conditions[cntConditions++] = trueAndFalseChilds[i]->ptrTrueParents[j]->isConditionMet;

		// Loop through current node child's false parents. Get list of their conditions.
		// For false parent, condition is met when parent condition is false
		for (j = 0; j < trueAndFalseChilds[i]->falseParentsCnt; j++)
			conditions[cntConditions++] = !trueAndFalseChilds[i]->ptrFalseParents[j]->isConditionMet;

		// Check conditions:
		//		+) when "&"  operator, there must not exists single false condition
		//		+) when "||" operator, there must be at least one true condition
		if (!common_node_exists(startNodes, trueAndFalseChilds[i], startNodesCnt)
			&& (strcmp(trueAndFalseChilds[i]->nodeOperator, "&") == 0 && !common_does_condition_exists(conditions, 0, cntConditions)
				|| strcmp(trueAndFalseChilds[i]->nodeOperator, "||") == 0 && common_does_condition_exists(conditions, 1, cntConditions)
				)
			)
			startNodes[startNodesCnt++] = trueAndFalseChilds[i];
	}

	// Handle disconnected nodes. Put all nodes on start list, because they are already evaluated in runtime, inside node executers
	for (i = 0; i < node->disconnectedNodesCnt; i++)
		startNodes[startNodesCnt++] = node->disconnectedNodes[i];

	// Inform triggering node that we are ready for next round
	for (i = 0; i < node->nodesToTriggerCnt; i++)
		common_pull_event_from_buffer(node->nodesToTrigger[i]);

	// Initialize stop node list
	Node *stopNodeList[100];
	int iStopNodeListCnt = 0;

	// Start nodes from start list
	StartOrSignalNodes(startNodes, startNodesCnt, stopNodeList, &iStopNodeListCnt);

	// Stop nodes
	for (i = 0; i < iStopNodeListCnt; i++)
		stopNodeList[i]->isEventActive = !stopNodeList[i]->unregisterEvent;

	if (node->disconnectedNodes != NULL)
	{
		free(node->disconnectedNodes);
		node->disconnectedNodes = NULL;
		node->disconnectedNodesCnt = 0;
	}

	strncpy(node->status, STATUS_STOPPED, strlen(STATUS_STOPPED) + 1);
	//printf("End Doing : %s\n", node->id);
}
//**************************************************************************/
//************************ END MAIN WF PROCEDURE ***************************/
//**************************************************************************/


//**************************************************************************/
//************************ START INI SETIINGS HANDLER **********************/
//**************************************************************************/
int engineConfigHandler(void* user, const char* section, const char* name, const char* value)
{
	EngineConfiguration* pconfig = (EngineConfiguration*)user;

#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
	if (MATCH("NetCore", "Path")) {
		pconfig->netCorePath = strdup(value);
	}
	else if (MATCH("Mqtt", "Host")) {
		pconfig->mqttHost = strdup(value);
	}
	else if (MATCH("Mqtt", "Port")) {
		pconfig->mqttPort = atoi(value);
	}
	else if (MATCH("Update", "UpdatedBy")) {
		pconfig->updatedBy = strdup(value);
	}
	else if (MATCH("Compile", "Clean")) {
		if (strcmp(value, "1") == 0)
		{
			char tmp_dir[256];
			printf("Clearing cache....\n");
			snprintf(tmp_dir, sizeof(tmp_dir), "%s%s", _working_directory, "/tmp");
			common_remove_directory(tmp_dir);
		}
	}
	else if (MATCH("Elements", "Version")) {
		pconfig->nodesVersion = strdup(value);
	}
	else if (MATCH("Template", "ID")) {
		pconfig->username = strdup(value);
		pconfig->projectId = strdup(value);
	}
	else if (MATCH("Template", "Key")) {
		pconfig->password = strdup(value);
	}
	else if (MATCH("Template", "WorkstationID")) {
		pconfig->workstationId = strdup(value);
	}
	else if (MATCH("Template", "WorkstationName")) {
		pconfig->workstationName = strdup(value);
	}
	else if (MATCH("Debug", "Enabled")) {
		pconfig->isDebugEnabled = 1; //atoi(value);
	}
	else if (MATCH("RemoteOperations", "Update")) {
		pconfig->isDebugEnabled = 1; //atoi(value);
	}
	else if (MATCH("RemoteOperations", "Restart")) {
		pconfig->isRemoteRestartEnabled = 1; //atoi(value);
	}
	else if (MATCH("RemoteOperations", "Info")) {
		pconfig->isRemoteInfoEnabled = 1; //atoi(value);
	}
	else {
		return 0;  /* unknown section/name, error */
	}
	return 1;
}

void ReadEngineConfiguration()
{
	if (ini_parse(_settings_file, engineConfigHandler, &engineConfiguration) < 0)
	{
		printf("Can't load zenodys engine configuration\n");
		exit(1);
	}
}
//**************************************************************************/
//************************ END INI SETIINGS HANDLER ************************/
//**************************************************************************/

/**
* Deletes node implementations that are no longer necessary.
* Remote update creates ObsoleteElements file with these nodes.
* When this file exists, remote update was triggered.
* Create UpdateProgress token to inform mqtt handler to send message about successfule remote update
*
* @return	none
*/
void DeleteObsoleteNodeFiles()
{
	FILE *pFile;
	int fsize = 0, i;
	char *fcontent = NULL;

	char obsolete_elements_file_path[MAX_PATH];
	snprintf(obsolete_elements_file_path, sizeof(obsolete_elements_file_path), "%s%s", _project_root, "/ObsoleteElements.zen");
	pFile = fopen(obsolete_elements_file_path, "r");

	if (pFile)
	{
		fseek(pFile, 0, SEEK_END);
		fsize = ftell(pFile);
		if (fsize > 0)
		{
			rewind(pFile);
			fcontent = (char*)malloc(sizeof(char) * fsize);
			fread(fcontent, 1, fsize, pFile);

			int numFiles = 0;
			char** files = NULL;
			common_str_split(fcontent, ",", &numFiles, &files);
			for (i = 0; i < numFiles; i++)
				remove(files[i]);

			common_free_splitted_string(files, numFiles);
		}
		fclose(pFile);
		remove(obsolete_elements_file_path);

		pFile = fopen("UpdateProgress", "w");
		fclose(pFile);
	}
}