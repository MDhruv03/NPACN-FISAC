#ifndef PROTOCOL_H
#define PROTOCOL_H

/*
    Message Protocol (JSON)

    Authentication:
    {
        "type": "auth",
        "payload": {
            "username": "string",
            "password": "string"
        },
        "timestamp": "integer"
    }

    Location Update:
    {
        "type": "location",
        "payload": {
            "latitude": "double",
            "longitude": "double"
        },
        "timestamp": "integer"
    }

    Subscription:
    {
        "type": "subscribe",
        "payload": {
            "channel": "string"
        },
        "timestamp": "integer"
    }
*/

#include "cJSON.h"

// Message types
#define MSG_TYPE_AUTH "auth"
#define MSG_TYPE_LOCATION "location"
#define MSG_TYPE_SUBSCRIBE "subscribe"

// Function to process an incoming message
void handle_message(int client_sock, const char *message);

// Function to log an event
void log_event(const char* level, const char* message);

#endif // PROTOCOL_H
