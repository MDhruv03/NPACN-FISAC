#ifndef PROTOCOL_H
#define PROTOCOL_H

/*
    Message Protocol (JSON over WebSocket)

    Authentication:
    {
        "type": "auth",
        "payload": {
            "username": "string",
            "password": "string"
        }
    }

    Authentication Response:
    {
        "type": "auth_response",
        "payload": {
            "success": true/false,
            "message": "string",
            "user_id": integer
        }
    }

    Register:
    {
        "type": "register",
        "payload": {
            "username": "string",
            "password": "string"
        }
    }

    Location Update:
    {
        "type": "location",
        "payload": {
            "latitude": double,
            "longitude": double,
            "userId": "string"
        }
    }

    Subscription:
    {
        "type": "subscribe",
        "payload": {
            "channel": "string"
        }
    }

    Error:
    {
        "type": "error",
        "payload": {
            "message": "string"
        }
    }
*/

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <winsock2.h>
#include "cJSON.h"
#include "server.h"

/* Message types */
#define MSG_TYPE_AUTH          "auth"
#define MSG_TYPE_AUTH_RESPONSE "auth_response"
#define MSG_TYPE_REGISTER     "register"
#define MSG_TYPE_LOCATION     "location"
#define MSG_TYPE_SUBSCRIBE    "subscribe"
#define MSG_TYPE_ERROR        "error"

/* Process an incoming message. client_info is mutable for auth state updates. */
void handle_message(ClientInfo *client_info, const char *message);

/* Log an event to the database */
void log_event(const char *level, const char *message);

#endif /* PROTOCOL_H */
