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

/*======================================================================
|
|       Main mqtt handler
|
+-----------------------------------------------------------------------
|
|   Known Bugs:		* none
|
|	     To Do:		* Visual debugging
*========================================================================*/

#include "dirent.h"
#ifdef WIN32
#include <windows.h>
#endif
#include "ZenMqtt.h"
#include "cJSON.h"
#include "b64.h"
#include "zip.h"

#define AsyncTestClient_initializer {NULL,NULL }
ClientCtx _client_ctx = AsyncTestClient_initializer;
MQTTAsync c;
MQTTAsync_connectOptions opts = MQTTAsync_connectOptions_initializer;
MQTTAsync_willOptions wopts = MQTTAsync_willOptions_initializer;
MQTTAsync_SSLOptions sslopts = MQTTAsync_SSLOptions_initializer;

char _topic_prefix[255] = "";

//**************************************************************************/
//************************ START MQTT CALLBACKS ****************************/
//**************************************************************************/

/**
* Callback on succesful mqtt connection
* When connected, send info payload and subscribe to system topics.
* Send remote update complete message, if engine restart was triggered by remote update
*
* @param	context		current client context
* @param	response	connection response
*
* @return	none
*/
void mqtt_on_connect(void* context, MQTTAsync_successData* response)
{
	char payload[PAYLOAD_LENGTH] = "";
	char callbackTopic[TOPIC_LENGTH] = "";
	char restartTopic[TOPIC_LENGTH] = "";

	ClientCtx* client = (ClientCtx*)context;

	subscribe_system_topics(client);

	printf("MQTT connection to %s succeed...\n", response->alt.connect.serverURI);

	get_infoGet_json(payload, callbackTopic, client);
	mqtt_send_zen_message(payload, callbackTopic, client);

	FILE *pFile = fopen("UpdateProgress", "r");
	if (pFile != NULL)
	{
		sprintf(restartTopic, "%s%s", _topic_prefix, "/info/updateRestartResponse");
		mqtt_send_zen_message(client->engineConfiguration.updatedBy, restartTopic, client);
		fclose(pFile);
		remove("UpdateProgress");
	}
}

/**
* Handler for all arrived messages
*
* @param	context			current client context
* @param	topicName		topic on which message arrived
* @param	topicLen		topic length
* @param	message			arrived message
*
* @return	none
*/
int mqtt_on_message_arrived(void* context, char* topicName, int topicLen, MQTTAsync_message* message)
{
	/*
						SIGNALS

		/debug/getSnapshot		->	start debugging session
		/debug/stop				->	stop debugging session
		/breakpoint/stop		->	step over
		/breakpoint/continue	->	next step
	*/

	char payload[PAYLOAD_LENGTH];
	char callbackTopic[TOPIC_LENGTH] = "";
	ClientCtx* client = (ClientCtx*)context;

	if (common_string_ends_with(topicName, "/update"))
		make_update(message, client);

	else if (common_string_ends_with(topicName, "/info/get"))
		get_infoGet_json(payload, callbackTopic, client);

	else if (common_string_ends_with(topicName, "/info/fileListRequest"))
		get_fileListResponse_json(payload, callbackTopic, client);

	else if (common_string_ends_with(topicName, "/debug/getSnapshot"))
		start_debugging_session(payload, callbackTopic);

	else if (common_string_ends_with(topicName, "/breakpoint/continue"))
		continue_with_breakpoint(payload, callbackTopic);

	else if (common_string_ends_with(topicName, "/breakpoint/stop") || common_string_ends_with(topicName, "/debug/stop"))
		stop_debugging_session();

	if (strcmp(callbackTopic, "") != 0)
		mqtt_send_zen_message(payload, callbackTopic, client);

	MQTTAsync_freeMessage(&message);
	MQTTAsync_free(topicName);
	return 1;
}

void mqtt_on_subscribe(void* context, MQTTAsync_successData* response)
{
}

void mqtt_on_connect_failure(void* context, MQTTAsync_failureData* response)
{
	ClientCtx* client = (ClientCtx*)context;
	printf("MQTT Error : on_connect_failure\n");
}

void mqtt_on_publish_failure(void* context, MQTTAsync_failureData* response)
{
	ClientCtx* client = (ClientCtx*)context;
	printf("MQTT Error : on_publish_failure\n");
}
//**************************************************************************/
//************************ END MQTT CALLBACKS ******************************/
//**************************************************************************/


//**************************************************************************/
//************************ START MESSAGE HANDLERS **************************/
//**************************************************************************/

/**
* Handle remote updates
*
* @param	message		base64 project files
* @param	client		current client context
*
* @return	none
*/
void make_update(MQTTAsync_message* message, ClientCtx* client)
{
	FILE * pFile;
	long lSize = 0;
	char zip_file[MAX_PATH];
	char zip_extraction_path[MAX_PATH];

	sprintf(zip_file, "%s%s", client->engineConfiguration.workingDir, "/project/zenodys.zip");
	sprintf(zip_extraction_path, "%s%s", client->engineConfiguration.workingDir, "/project/");

	// Save base64 payload to zenodys.zip file
	pFile = fopen(zip_file, "wb+");
	if (pFile == NULL)
		printf("Error in project remote update. Cannot create file zenodys.zip.\n");

	size_t *decoded_length = 0;
	char *dec = b64_decode_ex(message->payload, message->payloadlen, &decoded_length);

	if (0 == fwrite(dec, sizeof(char), decoded_length, pFile))
		printf("Error in project remote update. Cannot create file zenodys.zip.\n");

	fclose(pFile);

	//Extract zenodys.zip content
	int rc = common_zip_extract(zip_file, zip_extraction_path, 'wb+');
	if (rc != 0)
	{
		printf("Fatal error : problem with extracting zenodys.zip archive.");
		exit(1);
	}

	rc = remove(zip_file);
	make_restart(client);
}

void stop_debugging_session()
{
	common_set_debug_mode(0);
	common_signal_debug_condition();
}

void start_debugging_session(char payload[PAYLOAD_LENGTH], char topicName[TOPIC_LENGTH])
{
	common_set_debug_mode(1);
	continue_with_breakpoint(payload, topicName);
}

void continue_with_breakpoint(char payload[PAYLOAD_LENGTH], char topicName[TOPIC_LENGTH])
{
	common_signal_debug_condition();
	strncpy(payload, "", 1);
	sprintf(topicName, "%s%s", "/breakpoint/stepover", _topic_prefix);
}

void get_fileListResponse_json(char payload[PAYLOAD_LENGTH], char topicName[TOPIC_LENGTH], ClientCtx* client)
{
	cJSON *root, *filesJson;
	int iFilesCnt = 0, i;
	char** files;
	char* tmp;

	root = cJSON_CreateObject();
	filesJson = cJSON_CreateArray();

	cJSON_AddItemToObject(root, "ElementsVersion", cJSON_CreateString(client->engineConfiguration.nodesVersion));
	cJSON_AddItemToObject(root, "Files", filesJson);

	files = malloc(100000 * sizeof(char*));
	common_get_files(client->engineConfiguration.projectRoot, files, &iFilesCnt);

	for (i = 0; i < iFilesCnt; i++)
		cJSON_InsertItemInArray(filesJson, i, cJSON_CreateString(files[i]));

	tmp = cJSON_Print(root);
	strncpy(payload, tmp, strlen(tmp) + 1);
	free(tmp);
	free(files);

	sprintf(topicName, "%s%s", _topic_prefix, "/info/fileListResponse");
	cJSON_Delete(root);
}

void get_infoGet_json(char payload[PAYLOAD_LENGTH], char topicName[TOPIC_LENGTH], ClientCtx* client)
{
	cJSON *root;
	char* tmp;

	root = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "IsRemoteDebugEnabled", cJSON_CreateBool(client->engineConfiguration.isDebugEnabled ? cJSON_True : cJSON_False));
	cJSON_AddItemToObject(root, "IsRemoteInfoEnabled", cJSON_CreateBool(client->engineConfiguration.isRemoteInfoEnabled ? cJSON_True : cJSON_False));
	cJSON_AddItemToObject(root, "IsRemoteRestartEnabled", cJSON_CreateBool(client->engineConfiguration.isRemoteRestartEnabled ? cJSON_True : cJSON_False));
	cJSON_AddItemToObject(root, "IsRemoteUpdateEnabled", cJSON_CreateBool(client->engineConfiguration.isRemoteUpdateEnabled ? cJSON_True : cJSON_False));
	cJSON_AddItemToObject(root, "CoreVersion", cJSON_CreateString(client->engineConfiguration.engineVersion));
	cJSON_AddItemToObject(root, "ElementsVersion", cJSON_CreateString(client->engineConfiguration.nodesVersion));

	tmp = cJSON_Print(root);
	strncpy(payload, tmp, strlen(tmp) + 1);
	free(tmp);
	sprintf(topicName, "%s%s", _topic_prefix, "/info/send");
	cJSON_Delete(root);
}
//**************************************************************************/
//************************ END MESSAGE HANDLERS ****************************/
//**************************************************************************/

void make_restart(ClientCtx* client)
{
	char topic[255] = "";
	int rc;

	sprintf(topic, "%s%s", _topic_prefix, "/info/updateResponse");
	mqtt_send_zen_message("RESTARTING...", topic, client);

	MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
	rc = MQTTAsync_disconnect(c, &opts);

#ifdef WIN32

	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	BOOL rv = FALSE;
	WCHAR cmdline[] = TEXT("cmd.exe /c Restart.bat");

	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	// Add this if you want to hide or minimize the console
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE; //or SW_MINIMIZE
							  ///////////////////////////////////////////////////////
	memset(&pi, 0, sizeof(pi));
	rv = CreateProcess(NULL, cmdline, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi);

	if (rv) {
		WaitForSingleObject(pi.hProcess, INFINITE);
		printf("Restarting...\n");
	}
	else {
		printf("Restarting failed...\n");
	}
#else
	pid_t pid;
	pid = fork();
	if (pid == 0)
		execlp("./Restart.sh", NULL);
#endif
}

//**************************************************************************/
//************************ START MQTT HELPERS ******************************/
//**************************************************************************/

int mqtt_send_zen_message(char payload[PAYLOAD_LENGTH], char callbackTopic[TOPIC_LENGTH], ClientCtx* client)
{
	MQTTAsync_message pubmsg = MQTTAsync_message_initializer;
	MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;

	pubmsg.qos = 2;
	pubmsg.retained = 0;
	opts.onSuccess = NULL;
	opts.onFailure = mqtt_on_publish_failure;
	opts.context = client;

	pubmsg.payload = payload;
	pubmsg.payloadlen = (int)strlen(payload);
	return MQTTAsync_sendMessage(client->client, callbackTopic, &pubmsg, &opts);
}

/**
* Topic subscribe helper
*
* @param	topicPrefix		prefix for non default instances
* @param	topic			topic to subscribe to
* @param	context			current client context
*
* @return	none
*/
void subscribe_topic(char* topic, ClientCtx* context)
{
	MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
	char tmp[255] = "";

	sprintf(tmp, "%s%s", _topic_prefix, topic);
	opts.onSuccess = mqtt_on_subscribe;
	opts.context = context;

	int rc = MQTTAsync_subscribe(_client_ctx.client, tmp, 2, &opts);
	if (rc != MQTTASYNC_SUCCESS)
	{
		printf("Connection failed ... \n");
		exit(1);
	}
}

/*
void set_topic_prefix(char* topicPrefix)
{

}*/
//**************************************************************************/
//************************ END MQTT HELPERS ******************************/
//**************************************************************************/


/**
* Subscribes to system topics
*
* @param	context		current client context
*
* @return	none
*/
void subscribe_system_topics(ClientCtx* context)
{
	subscribe_topic("/info/get", context);
	subscribe_topic("/logOff", context);

	if (context->engineConfiguration.isDebugEnabled)
	{
		subscribe_topic("/debug/get", context);
		subscribe_topic("/debug/getSnapshot", context);
		subscribe_topic("/debug/stop", context);
		subscribe_topic("/breakpoint/continue", context);
		subscribe_topic("/breakpoint/stop", context);
		subscribe_topic("/debug/logOff", context);
		subscribe_topic("/info/fileListRequest", context);
	}

	//if (context->engineConfiguration.isRemoteUpdateEnabled)
	subscribe_topic("/update", context);

	if (context->engineConfiguration.isRemoteRestartEnabled)
		subscribe_topic("/restart", context);

	if (context->engineConfiguration.isRemoteInfoEnabled)
		subscribe_topic("/info/gatewayRequest", context);
}

/**
* Starts mqtt connection
*
* @param	engine_configuration	configuration of current engine instance
*
* @return	none
*/
EXTERN_DLL_EXPORT void ConnectMqtt(EngineConfiguration engine_configuration)
{
	char client_id[GUID_LENGTH] = "";
	char serverURI[50] = "";
	char willTopicName[50];
	int rc = 0;

	if (strcmp(engine_configuration.mqttHost, "") == 0)
		return;

	// Set topic for non default instances
	if (strcmp(engine_configuration.workstationId, "") == 0)
		sprintf(_topic_prefix, "%s%s", "/", engine_configuration.projectId);
	else
		sprintf(_topic_prefix, "%s%s%s%s", "/", engine_configuration.projectId, "/", engine_configuration.workstationId);

	sprintf(willTopicName, "%s%s", _topic_prefix, "/shutDown");

	common_generate_guid(client_id, 4);

	// Make complete mqtt host (protocol://host:port)
	sprintf(serverURI, "%s%s%s%d", "ssl://", engine_configuration.mqttHost, ":", engine_configuration.mqttPort);

	rc = MQTTAsync_create(&c, serverURI, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);
	if (rc != MQTTASYNC_SUCCESS)
	{
		MQTTAsync_destroy(&c);
		printf("Failed to create mqtt connection.\n");
		exit(1);
	}

	_client_ctx.client = c;
	_client_ctx.engineConfiguration = engine_configuration;
	rc = MQTTAsync_setCallbacks(c, &_client_ctx, NULL, mqtt_on_message_arrived, NULL);

	opts.keepAliveInterval = 20;
	opts.cleansession = 1;
	opts.username = engine_configuration.username;
	opts.password = engine_configuration.password;
	opts.automaticReconnect = 1;

	opts.will = &wopts;
	opts.will->message = engine_configuration.projectId;
	opts.will->qos = 2;
	opts.will->retained = 0;
	opts.will->topicName = willTopicName;

	opts.onSuccess = mqtt_on_connect;
	opts.onFailure = mqtt_on_connect_failure;
	opts.context = &_client_ctx;

	opts.ssl = &sslopts;
	opts.ssl->enableServerCertAuth = 0;

	rc = MQTTAsync_connect(c, &opts);
	if (rc != MQTTASYNC_SUCCESS)
	{
		printf("Mqtt connection failed.\n");
		exit(1);
	}
}