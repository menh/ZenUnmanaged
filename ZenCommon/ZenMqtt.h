#pragma once
#include "MQTTAsync.h"
#include "ZenCommon.h"

#define TOPIC_LENGTH 512
#define PAYLOAD_LENGTH 100000

typedef struct
{
	MQTTAsync client;
	EngineConfiguration engineConfiguration;
} ClientCtx;

void start_debugging_session(char payload[PAYLOAD_LENGTH], char topicName[TOPIC_LENGTH]);
void stop_debugging_session();
void continue_with_breakpoint();
void subscribe_system_topics(ClientCtx* context);
void mqtt_on_connect(void* context, MQTTAsync_successData* response);
int mqtt_on_message_arrived(void* context, char* topicName, int topicLen, MQTTAsync_message* message);
void mqtt_on_subscribe(void* context, MQTTAsync_successData* response);
void mqtt_on_connect_failure(void* context, MQTTAsync_failureData* response);
void mqtt_on_publish_failure(void* context, MQTTAsync_failureData* response);
void make_update(MQTTAsync_message* message, ClientCtx* client);
void get_fileListResponse_json(char payload[PAYLOAD_LENGTH], char topicName[TOPIC_LENGTH], ClientCtx* client);
void get_infoGet_json(char payload[PAYLOAD_LENGTH], char topicName[TOPIC_LENGTH], ClientCtx* client);
void make_restart(ClientCtx* client);
int mqtt_send_zen_message(char payload[PAYLOAD_LENGTH], char callbackTopic[TOPIC_LENGTH], ClientCtx* client);
void subscribe_system_topics(ClientCtx* context);
