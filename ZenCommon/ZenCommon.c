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

/*==================================================================================
|
|       	Shared lib between Elements and Zeno Engine:
|				* Contains common settings (project root, project id, node list...)
|				* String helpers (splitting string....)
|				* Operations on Elements list (getting element results, 
|				  result infos, properties....)
|				* Event handling (buffering events, pulling events by event triggers)
|
+-----------------------------------------------------------------------------------
|					
|   Known Bugs:		* none
|
|	     To Do:		* handle when triggering node (eg OpcUa) has no trigger nodes
|					* fix all statements involving lastResult
|					  (eg eventParams->node->lastResult = *(int*)eventParams->data
|					  to  *((int*)(node->lastResult)) = currentCounterValue)
|
*=====================================================================================================*/

#include "ZenCommon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dirent.h"
#include "cJSON.h"
#include <sys/stat.h>

Node**				_nodeList;
int					_nodeListLength;
unsigned			_isDebugMode;
char				_project_root[256];
char				_project_id[256];
ptrExecNode			_execNodeFunct;
EngineConfiguration _engineConfiguration;

EXTERN_DLL_EXPORT char* COMMON_PROJECT_ROOT{ return (_project_root); }
EXTERN_DLL_EXPORT char* COMMON_PROJECT_ID{ return (_project_id); }
EXTERN_DLL_EXPORT Node** COMMON_NODE_LIST{ return (_nodeList); }
EXTERN_DLL_EXPORT int COMMON_NODE_LIST_LENGTH{ return _nodeListLength; }
EXTERN_DLL_EXPORT EngineConfiguration COMMON_ENGINE_CONFIGURATION{ return _engineConfiguration; }

pthread_cond_t _pause_node_conditions[1000];
int _pause_node_conditions_cnt = 0;

pthread_mutex_t _event_queue_locks[1000];
int _event_queue_locks_cnt = 0;

// Visual breakpoints handlers
pthread_cond_t _debug_cond;
pthread_mutex_t _debug_mutex = PTHREAD_MUTEX_INITIALIZER;


//************************ Start debug operations **************************/
/**
* Signals visual breakpoint
*
* @return	void
*/
EXTERN_DLL_EXPORT void common_signal_debug_condition()
{
	pthread_cond_signal(&_debug_cond);
}

/**
* Pauses visual breakpoint
*
* @return	void
*/
EXTERN_DLL_EXPORT void common_wait_debug_signal()
{
	pthread_cond_wait(&_debug_cond, &_debug_mutex);
}

/**
* Gets debug mode state
*
* @return	Returns 1 if debug mode is enabled, otherwise 0
*/
EXTERN_DLL_EXPORT unsigned common_is_debug_mode_enabled()
{
	return _isDebugMode;
}

/**
* Sets debug mode
*
* @param isDebugMode	1 - visual breakpoints are enabled, otherwise not
* @return				void
*/
EXTERN_DLL_EXPORT void common_set_debug_mode(unsigned isDebugMode)
{
	_isDebugMode = isDebugMode;
}
//************************ End debug operations **************************/


//************************ Start node operations **************************/

/**
* Initialize node list
*
* @param nodeListCnt	length of node list
* @return				void
*/
EXTERN_DLL_EXPORT void common_initialize_node_list(int nodeListCnt)
{
	_nodeList = malloc(nodeListCnt * sizeof(Node*));
}

/**
* Adds node to node list
*
* @param node		node to add in the list
* @return           void
*/
EXTERN_DLL_EXPORT void common_add_node_to_list(Node* node)
{
	_nodeList[_nodeListLength++] = node;
}

/**
* Check if node exists in given array
*
* @param nodeArr	nodes array to be checked
* @param node		node to check
* @param listCnt	length of array to be checked
* @return           true if array contains node, otherwise false
*/
EXTERN_DLL_EXPORT int common_node_exists(Node** nodeArr, Node* node, int listCnt)
{
	int i;
	for (i = 0; i < listCnt; i++)
		if (strcmp(nodeArr[i]->id, node->id) == 0)
			return 1;

	return 0;
}

/**
* Sets value of node argument
*
* @param nodeId		unique identifier of node that argument value is setted
* @param key		key
* @param key		value
* @return			void
*/
EXTERN_DLL_EXPORT void  common_set_node_arg(Node *node, char* key, char* value)
{
	int i;
	for (i = 0; i < node->argsCnt; i++)
	{
		if (strcmp(key, node->args[i]->Key) == 0)
		{
			strncpy(node->args[i]->Value, value, strlen(value) + 1);
			return;
		}
	}
}

/**
* Gets value of node argument
*
* @param nodeId		unique identifier of node that argument value must be searched
* @param key		key of argument value
* @return			string format of argument value
*/
EXTERN_DLL_EXPORT char* common_get_node_arg(Node* node, char* key)
{
	int i;
	for (i = 0; i < node->argsCnt; i++)
	{
		if (strcmp(key, node->args[i]->Key) == 0)
			return node->args[i]->Value;
	}
	return "";
}

/**
* Executes node.
* Demand for node execution can be from managed or unmanaged code.
* Callback implemented on engine must be called
*
* @param node		node which is going to be executed
* @return           void
*/
EXTERN_DLL_EXPORT void common_exec_node(Node* node)
{
	_execNodeFunct(node);
}
//************************ End node operations   **************************/

//*************************************************************************/
//*********************** Start Event Buffer Handling *********************/
//*************************************************************************/
/**
* Two terms are involved in event handling procedure:
*		+ TRIGGER NODE		: node that triggers triggering node (eg "Debug")
*		+ TRIGGERING NODE	: node that is triggered by trigger node (eg "OpcUaClientSubs"). This are event generators.
*
*
* Event buffer handling includes:
*	+ Buffering events from event generators
*	+ Pulling events from buffer on trigger nodes completion. When trigger node completes, it sets result from buffer and signal trgiggering node's thread
*
*	 TRIGGERING NODE (OpcUaClientSubs)		   TRIGGER NODE (Debug1)
*     ____							            _____
*    |____| --------> EVENT BUFFER <---------- |_____|
*
* On left side, there are event generators, (OPC UA, Button nodes...). Events are saved into event sink buffer if their generation is higher then consumption.
* On the right side they are trigger nodes, that are pulling events from buffer, and fireing eventable nodes
*/

/**
* Triggering node (eg OpcUa) is called from trigger node (eg Debug). Function sets result and signal's triggering node's thread.
*
* @param node			triggered node

* @return				void
*/
EXTERN_DLL_EXPORT void common_pull_event_from_buffer(Node* node)
{
	pthread_mutex_lock(&_event_queue_locks[node->eventQueueLockId]);

	node->hasGreenLight = 1;
	if (node->bufferedEvents.count > 0)
	{
		switch (node->lastResultType) 
		{
			case RESULT_TYPE_INT:
				node->lastResult = (int*)popqueue(&node->bufferedEvents);
				break;
			
			case RESULT_TYPE_CHAR_ARRAY:
				node->lastResult = (char*)popqueue(&node->bufferedEvents);
				break;
		}
		pthread_cond_signal(&_pause_node_conditions[node->pauseNodeConditionId]);
		//printf("%d events after pull\n", node->bufferedEventsCount);
	}
	pthread_mutex_unlock(&_event_queue_locks[node->eventQueueLockId]);
}

/**
* Called from event generator (eg OpcClientSubs). Workflow is signalled with current result when buffer is empty and loop is not busy.
* Otherwise, event is saved to buffer queue.
*
* @param context	struct with node and result info
* @return			void
*/
EXTERN_DLL_EXPORT void common_push_event_to_buffer(void *context)
{
	struct eventContextParamsStruct *eventParams = context;
	pthread_mutex_lock(&_event_queue_locks[eventParams->node->eventQueueLockId]);

	// Node has not yet been initialized (workflow didn't visit yet our node). In this case ignore it and do not fill any buffer yet
	// Beware to not come before pausing node. Look at the ZenEngine....
	if (!eventParams->node->isEventActive)
	{
		pthread_mutex_unlock(&_event_queue_locks[eventParams->node->eventQueueLockId]);
		return;
	}

	if (eventParams->node->bufferedEvents.count == 0 && eventParams->node->hasGreenLight)
	{
		eventParams->node->hasGreenLight = 0;
		switch (eventParams->node->lastResultType) 
		{
			case RESULT_TYPE_INT:
				eventParams->node->lastResult = *(int*)eventParams->data;
				break;
			
			case RESULT_TYPE_CHAR_ARRAY:
				eventParams->node->lastResult = *(char*)eventParams->data;
				break;
		}
		pthread_cond_signal(&_pause_node_conditions[eventParams->node->pauseNodeConditionId]);
	}
	else
	{
		switch (eventParams->node->lastResultType) 
		{
			case RESULT_TYPE_INT:
				push(&eventParams->node->bufferedEvents, *(int*)eventParams->data);
				break;

			case RESULT_TYPE_CHAR_ARRAY:
				push(&eventParams->node->bufferedEvents, *(char*)eventParams->data);
				break;
		}
	}
	//printf("There are %d events after push...\n", eventParams->node->bufferedEventsCount);
	pthread_mutex_unlock(&_event_queue_locks[eventParams->node->eventQueueLockId]);
}

/**
* Inits event queue lock. LockId is saved into nodes eventQueueLockId field
*
* @param node		node, which lock is going to be inited
* @return			void
*/
EXTERN_DLL_EXPORT void common_init_event_queue_lock(Node* node)
{
	pthread_mutex_init(&_event_queue_locks[_event_queue_locks_cnt++], NULL);
	node->eventQueueLockId = _event_queue_locks_cnt - 1;
}

/**
* Inits pause condition. Pause condition Id is returned back to node
*
* @return	int		Pause condition id
*/
EXTERN_DLL_EXPORT int common_init_pause_condition()
{
	pthread_cond_init(&_pause_node_conditions[_pause_node_conditions_cnt++], NULL);
	return _pause_node_conditions_cnt - 1;
}

/**
* Signals pause condition.
*
* @param	pauseNodeConditionId	condition id that is saved in node's condition id field
* @return	void
*/
EXTERN_DLL_EXPORT void common_signal_pause_condition(int pauseNodeConditionId)
{
	pthread_cond_signal(&_pause_node_conditions[pauseNodeConditionId]);
}

/**
* Pauses node main loop.
*
* @param	node				node, which loop is going to be paused
* @param	pause_node_mutex	mutext, required as pthread_cond_wait parameter
*
* @return	void
*/
EXTERN_DLL_EXPORT void common_wait_pause_condition(Node* node, pthread_mutex_t *pause_node_mutex)
{
	pthread_cond_wait(&_pause_node_conditions[node->pauseNodeConditionId], pause_node_mutex);
}

//*************************************************************************/
//*********************** End Event Buffer Handling   *********************/
//*************************************************************************/

/**
* Saves project related data
*
* @param project_root	root folder where project resides
* @param project_id		project unique identifier
* @return				void
*/
EXTERN_DLL_EXPORT void common_init_project(char* project_root, char* project_id, EngineConfiguration engineConfiguration, ptrExecNode execNodeFunct)
{
	strncpy(_project_root, project_root, strlen(project_root) + 1);
	strncpy(_project_id, project_id, strlen(project_id) + 1);

	_execNodeFunct = execNodeFunct;
	_engineConfiguration = engineConfiguration;
	pthread_cond_init(&_debug_cond, NULL);
	pthread_mutex_lock(&_debug_mutex);
}

/**
* Finds node by id
*
* @param	id		node with this Id will be searched
* @return   Node	node with provided Id
*/
EXTERN_DLL_EXPORT Node* common_get_node_by_id(char* id)
{
	int i;
	for (i = 0; i < COMMON_NODE_LIST_LENGTH; i++)
	{
		if (strcmp(id, COMMON_NODE_LIST[i]->id) == 0)
			return COMMON_NODE_LIST[i];
	}
	return NULL;
}

//******************* Start string helpers **********************/

/**
* Splits string by delimiter.
*
* @param str		string to split
* @param delim		delimiter
* @param numtokens	output variable with number of splitted tokens
* @param tokens		splitted tokens
* @return           void
*/
EXTERN_DLL_EXPORT void  common_str_split(const char* str, const char* delim, int* numtokens, char*** tokens)
{
	char *s = strdup(str);

	int tokens_alloc = 1;
	int tokens_used = 0;
	*tokens = calloc(tokens_alloc, sizeof(char*));
	char *token, *rest = s;
	while ((token = mystrsep(&rest, delim)) != NULL) {
		if (tokens_used == tokens_alloc) {
			tokens_alloc *= 2;
			*tokens = realloc(*tokens, tokens_alloc * sizeof(char*));
		}

		(*tokens)[tokens_used++] = strdup(token);
	}
	if (tokens_used == 0) {
		free(*tokens);
		*tokens = NULL;
	}
	else {
		*tokens = realloc(*tokens, tokens_used * sizeof(char*));
	}
	*numtokens = tokens_used;
	free(s);
}

/**
* Frees splitted string array
*
* @param tokens		tokens array
* @param cnt		tokens array length
* @return           void
*/
EXTERN_DLL_EXPORT void common_free_splitted_string(char** tokens, int cnt)
{
	int i;
	for (i = 0; i < cnt; i++)
		free(tokens[i]);

	free(tokens);
}

char* mystrsep(char** stringp, const char* delim)
{
	char* start = *stringp;
	char* p;

	p = (start != NULL) ? strpbrk(start, delim) : NULL;

	if (p == NULL)
	{
		*stringp = NULL;
	}
	else
	{
		*p = '\0';
		*stringp = p + 1;
	}
	return start;
}
//******************* End string helpers   **********************/


/**
* Parses string that contain nodes and populates node array
*
* @param nodeArray     array to be populated with nodes
* @param nodesString   nodes in string format
* @return              void
*/
EXTERN_DLL_EXPORT void common_parse_nodes(Node **nodeArray, char **nodesString, int cnt)
{
	int i, j;
	for (i = 0; i < cnt; i++)
	{
		for (j = 0; j < COMMON_NODE_LIST_LENGTH; j++)
		{
			if (strcmp(nodesString[i], COMMON_NODE_LIST[j]->id) == 0)
				nodeArray[i] = COMMON_NODE_LIST[j];
		}
	}
}

/**
* Check if array contains given string
*
* @param arr		string array to be checked
* @param str		string to check
* @param arrLength	length of array to be checked
* @return           true if array contains string, otherwise false
*/
EXTERN_DLL_EXPORT int common_is_string_in_array(char** arr, char* str, int arrLength)
{
	int i;
	for (i = 0; i < arrLength; i++)
		if (strcmp(arr[i], str) == 0)
			return 1;
	return 0;
}

/**
* Check if condition exists in given array of conditions
*
* @param conditions		array of conditions to be checked
* @param condition		condition to check
* @param conditionCnt	length of array to be checked
* @return				true if array contains condition, otherwise false
*/
EXTERN_DLL_EXPORT int common_does_condition_exists(int conditions[], int condition, int conditionCnt)
{
	int i;
	for (i = 0; i < conditionCnt; i++)
		if (conditions[i] == condition)
			return 1;

	return 0;
}

/**
* Generate unique identifier
*
* @param guid				output guid parameter
* @param number_of_blocks	number of blocks contained in guid
* @return					none
*/
EXTERN_DLL_EXPORT void common_generate_guid(char guid[GUID_LENGTH], int number_of_blocks)
{
	char tmp[GUID_LENGTH];
	int r, b;
	srand((unsigned)time(NULL));
	for (b = 0; b < number_of_blocks; b++)
	{
		r = rand();
		if (b == number_of_blocks - 1)
			snprintf(tmp, sizeof(tmp), "%d", r);
		else
			snprintf(tmp, sizeof(tmp), "%d-", r);

		strcat(guid, tmp);
	}
}

/**
* Checks if string ends with suffix
*
* @param str		string to check
* @param suffix		suffix
* @return			0 if not, otherwise non zero
*/
EXTERN_DLL_EXPORT int common_string_ends_with(const char *str, const char *suffix)
{
	if (!str || !suffix)
		return 0;
	size_t lenstr = strlen(str);
	size_t lensuffix = strlen(suffix);
	if (lensuffix > lenstr)
		return 0;
	return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

/**
* Get files recursively from given folder
*
* @param path	path to the directory
* @param files	founded files
* @param files	number of founded files
*
* @return		void
*/
EXTERN_DLL_EXPORT  void common_get_files(const char *path, char** files, int* filesCnt)
{
	DIR *d = opendir(path);
	size_t path_len = strlen(path);

	if (d)
	{
		struct dirent *p;
		while (p = readdir(d))
		{
			char *buf;
			size_t len;

			/* Skip the names "." and ".." as we don't want to recurse on them. */
			if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
				continue;

			len = path_len + strlen(p->d_name) + 2;
			buf = malloc(len);

			if (buf)
			{
				struct stat statbuf;

				snprintf(buf, len, "%s/%s", path, p->d_name);
				if (!stat(buf, &statbuf))
				{
					if (S_ISDIR(statbuf.st_mode))
						common_get_files(buf, files, filesCnt);
					else
					{
						files[*filesCnt] = malloc(256 * sizeof(char));
						strncpy(files[*filesCnt], buf, strlen(buf) + 1);
						(*filesCnt)++;
					}
				}
				free(buf);
			}
		}
	}
}

/**
* Reads line from file stream
*
* @param path	path to the directory
* @return		0 on succeed, non zero otherwise
*/
EXTERN_DLL_EXPORT size_t common_getline(char **lineptr, size_t *n, FILE *stream)
{
	char *bufptr = NULL;
	char *p = bufptr;
	size_t size;
	int c;

	if (lineptr == NULL) {
		return -1;
	}
	if (stream == NULL) {
		return -1;
	}
	if (n == NULL) {
		return -1;
	}
	bufptr = *lineptr;
	size = *n;

	c = fgetc(stream);
	if (c == EOF) {
		return -1;
	}
	if (bufptr == NULL) {
		bufptr = malloc(128);
		if (bufptr == NULL) {
			return -1;
		}
		size = 128;
	}
	p = bufptr;
	while (c != EOF) {
		if ((p - bufptr) > (size - 1)) {
			size = size + 128;
			bufptr = realloc(bufptr, size);
			if (bufptr == NULL) {
				return -1;
			}
		}
		*p++ = c;
		if (c == '\n') {
			break;
		}
		c = fgetc(stream);
	}

	*p++ = '\0';
	*lineptr = bufptr;
	*n = size;

	return p - bufptr - 1;
}

/**
* Checks if directory exists
*
* @param path	path to the directory
* @return		0 on not exists, 1 exists
*/
EXTERN_DLL_EXPORT int common_directory_exists(const char *path)
{
	int rc = 0;
	DIR *d = opendir(path);

	if (d)
	{
		rc = 1;
		closedir(d);
	}
	return rc;

}

/**
* Removes non empty directory
*
* @param path	path to the directory
* @return		0 on succeed, non zero otherwise
*/
EXTERN_DLL_EXPORT  int common_remove_directory(const char *path)
{
	DIR *d = opendir(path);
	size_t path_len = strlen(path);
	int r = -1;

	if (d)
	{
		struct dirent *p;

		r = 0;

		while (!r && (p = readdir(d)))
		{
			int r2 = -1;
			char *buf;
			size_t len;

			/* Skip the names "." and ".." as we don't want to recurse on them. */
			if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
			{
				continue;
			}

			len = path_len + strlen(p->d_name) + 2;
			buf = malloc(len);

			if (buf)
			{
				struct stat statbuf;

				snprintf(buf, len, "%s/%s", path, p->d_name);

				if (!stat(buf, &statbuf))
				{
					if (S_ISDIR(statbuf.st_mode))
					{
						r2 = common_remove_directory(buf);
					}
					else
					{
						r2 = unlink(buf);
					}
				}

				free(buf);
			}

			r = r2;
		}

		closedir(d);
	}

	if (!r)
	{
		r = rmdir(path);
	}

	return r;
}

/**
* Extracts zip file
*
* @param	zip_name	path to zip file
* @param	dir			directory, where zip is going to be extracted
* @param	arg			zip args
*
* @return	0 on succes extraction, otherwise non zero

*/
EXTERN_DLL_EXPORT int common_zip_extract(const char* zip_name, const char* dir, void* arg)
{
	return zip_extract(zip_name, dir, NULL, 'b');
}

//**************************************************************************/
//************************ START BUFFER HELPERS* ***************************/
//**************************************************************************/

EXTERN_DLL_EXPORT void common_init_buffer(buffer_t *buffer, int size) {
	buffer->size = size;
	buffer->start = 0;
	buffer->count = 0;
	buffer->element = malloc(sizeof(buffer->element)*size);
	/* allocated array of void pointers. Same as below */
	//buffer->element = malloc(sizeof(void *) * size);

}

int full(buffer_t *buffer) {
	if (buffer->count == buffer->size) {
		return 1;
	}
	else {
		return 0;
	}
}

int empty(buffer_t *buffer) {
	if (buffer->count == 0) {
		return 1;
	}
	else {
		return 0;
	}
}

void push(buffer_t *buffer, void *data) {
	int index;
	if (full(buffer)) {
		printf("Buffer overflow\n");
	}
	else {
		index = buffer->start + buffer->count++;
		if (index >= buffer->size) {
			index = 0;
		}
		buffer->element[index] = data;
	}
}


void * popqueue(buffer_t *buffer) {
	void * element;
	if (empty(buffer)) {
		printf("Buffer underflow\n");
		return "0";
	}
	else {
		/* FIFO implementation */
		element = buffer->element[buffer->start];
		buffer->start++;
		buffer->count--;
		if (buffer->start == buffer->size) {
			buffer->start = 0;
		}

		return element;
	}
}

void * popstack(buffer_t *buffer) {
	int index;
	if (empty(buffer)) {
		printf("Buffer underflow\n");
		return "0";
	}
	else {
		/* LIFO implementation */
		index = buffer->start + buffer->count - 1;
		if (index >= buffer->size) {
			index = buffer->count - buffer->size - 1;
		}
		buffer->count--;
		return buffer->element[index];
	}
}
//**************************************************************************/
//************************ END BUFFER HELPERS ******************************/
//**************************************************************************/

EXTERN_DLL_EXPORT void common_json_dump_table(Tables *tables)
{
	cJSON *root, *context, *events, *event, *header, *payload, *nonname, *tmpJson;

	char* tmp;
	int i, j, k;

	root = cJSON_CreateArray();
	cJSON *json_device;
	for (k = 0; k < tables->tablesCount; k++)
	{
		cJSON_AddItemToArray(root, event = cJSON_CreateObject());
		cJSON_AddStringToObject(event, "stream_id", "temperature");
		cJSON_AddStringToObject(event, "hash", "0x63d32b47");
		cJSON_AddItemToArray(event, events = cJSON_CreateArray());

		for (i = 0; i < tables->tables[k]->rowsCount; i++)
		{
			json_device = cJSON_CreateObject();

			for (j = 0; j < tables->tables[k]->colsCount; j++)
			{
				switch (tables->tables[k]->cols[j]->type) {

				case RESULT_TYPE_INT:
					cJSON_AddItemToObject(json_device, tables->tables[k]->cols[j]->name, cJSON_CreateNumber((int)tables->tables[k]->cols[j]->rows[i]));
					break;
				case RESULT_TYPE_CHAR_ARRAY:
					cJSON_AddItemToObject(json_device, tables->tables[k]->cols[j]->name, cJSON_CreateString(tables->tables[k]->cols[j]->rows[i]));
					break;
				}
			}
			cJSON_AddItemToArray(events, json_device);
		}
	}
	tmp = cJSON_Print(root);
}

Table* createTbl()
{
	int rowsNmb = 2;
	int colsNmb = 2;

	Table *table;
	table = malloc(1 * sizeof(Table*));
	table->colsCount = colsNmb;
	table->rowsCount = rowsNmb;

	table->cols = malloc(colsNmb * sizeof(char*));
	table->cols[0] = malloc(10 * sizeof(col*));
	table->cols[1] = malloc(10 * sizeof(col*));

	table->cols[0]->rows = malloc(rowsNmb * sizeof(int*));
	table->cols[1]->rows = malloc(rowsNmb * sizeof(char*));

	table->cols[0]->rows[0] = malloc(sizeof(int));
	table->cols[0]->rows[1] = malloc(10 * sizeof(char));

	table->cols[1]->rows[0] = malloc(sizeof(int));
	table->cols[1]->rows[1] = malloc(10 * sizeof(char));

	strcpy(table->cols[0]->name, "Temperature");
	table->cols[0]->type = RESULT_TYPE_INT;

	strcpy(table->cols[1]->name, "Label");
	table->cols[1]->type = RESULT_TYPE_CHAR_ARRAY;


	table->cols[0]->rows[0] = 999;
	strcpy(table->cols[1]->rows[0], "hey");

	table->cols[0]->rows[1] = 88;
	strcpy(table->cols[1]->rows[1], "hey1");
	return table;
}

EXTERN_DLL_EXPORT void TestDump()
{
	Tables* tables = malloc(sizeof(Tables));
	tables->tables = malloc(2 * sizeof(Table*));
	tables->tables[0] = malloc(sizeof(Table));
	tables->tables[1] = malloc(sizeof(Table));
	tables->tablesCount = 2;

	tables->tables[0] = createTbl();
	tables->tables[1] = createTbl();

	common_json_dump_table(tables);
}