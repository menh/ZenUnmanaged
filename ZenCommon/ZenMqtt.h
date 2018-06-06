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
