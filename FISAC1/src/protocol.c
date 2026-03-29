/*
    protocol.c - Application-level message handling.

    Processes JSON messages received over WebSocket:
    - Authentication (login with username/password)
    - Registration (create new user account)
    - Location updates (store and broadcast coordinates)
    - Subscriptions (channel-based filtering)

    Database operations are handled by a separate Python Flask service
    running on port 5000 via local HTTP POST requests.
*/

#include "protocol.h"
#include "http_client.h"
#include "websocket.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * send_json_response: Helper to send a JSON object as a WebSocket text frame.
 */
static void send_json_response(SOCKET sock, cJSON *json) {
    char *str = cJSON_PrintUnformatted(json);
    if (str) {
        websocket_frame_send(sock, str, (uint64_t)strlen(str), 1);
        free(str);
    }
}

/*
 * handle_auth: Process an authentication message.
 *
 * Validates username/password against the Python backend.
 */
static void handle_auth(ClientInfo *client, cJSON *payload) {
    cJSON *username = cJSON_GetObjectItem(payload, "username");
    cJSON *password = cJSON_GetObjectItem(payload, "password");

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", MSG_TYPE_AUTH_RESPONSE);
    cJSON *resp_payload = cJSON_CreateObject();
    cJSON_AddItemToObject(response, "payload", resp_payload);

    if (!username || username->type != cJSON_String || !password || password->type != cJSON_String) {
        cJSON_AddBoolToObject(resp_payload, "success", 0);
        cJSON_AddStringToObject(resp_payload, "message", "Missing username or password");
        send_json_response(client->sock, response);
        cJSON_Delete(response);
        return;
    }

    /* Serialize auth request for Python backend */
    char req_json[512];
    snprintf(req_json, sizeof(req_json), "{\"username\":\"%s\",\"password\":\"%s\"}", 
             username->valuestring, password->valuestring);

    char resp_json[1024];
    int success = 0;
    int user_id = -1;

    if (http_post_json("/auth", req_json, resp_json, sizeof(resp_json)) == 0) {
        cJSON *parsed_resp = cJSON_Parse(resp_json);
        if (parsed_resp) {
            cJSON *succ = cJSON_GetObjectItem(parsed_resp, "success");
            cJSON *uid = cJSON_GetObjectItem(parsed_resp, "user_id");
            if (succ && (succ->type == cJSON_True || succ->valueint == 1) && uid && uid->type == cJSON_Number) {
                success = 1;
                user_id = uid->valueint;
            }
            cJSON_Delete(parsed_resp);
        }
    }

    if (success) {
        client->authenticated = 1;
        client->user_id = user_id;
        strncpy(client->username, username->valuestring, MAX_USERNAME - 1);
        client->username[MAX_USERNAME - 1] = '\0';

        cJSON_AddBoolToObject(resp_payload, "success", 1);
        cJSON_AddStringToObject(resp_payload, "message", "Authentication successful");
        cJSON_AddNumberToObject(resp_payload, "user_id", user_id);
        cJSON_AddStringToObject(resp_payload, "username", client->username);

        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "User authenticated: %s (id=%d)", client->username, user_id);
        log_event("INFO", log_msg);
        printf("[AUTH] %s\n", log_msg);
    } else {
        cJSON_AddBoolToObject(resp_payload, "success", 0);
        cJSON_AddStringToObject(resp_payload, "message", "Invalid credentials or service unavailable");

        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "Failed auth attempt: %s", username->valuestring);
        log_event("WARN", log_msg);
        printf("[AUTH] %s\n", log_msg);
    }

    send_json_response(client->sock, response);
    cJSON_Delete(response);
}

/*
 * handle_register: Process a registration message.
 */
static void handle_register(ClientInfo *client, cJSON *payload) {
    cJSON *username = cJSON_GetObjectItem(payload, "username");
    cJSON *password = cJSON_GetObjectItem(payload, "password");

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", MSG_TYPE_AUTH_RESPONSE);
    cJSON *resp_payload = cJSON_CreateObject();
    cJSON_AddItemToObject(response, "payload", resp_payload);

    if (!username || username->type != cJSON_String || !password || password->type != cJSON_String) {
        cJSON_AddBoolToObject(resp_payload, "success", 0);
        cJSON_AddStringToObject(resp_payload, "message", "Missing username or password");
        send_json_response(client->sock, response);
        cJSON_Delete(response);
        return;
    }

    if (strlen(username->valuestring) < 2 || strlen(username->valuestring) > 50 || strlen(password->valuestring) < 3) {
        cJSON_AddBoolToObject(resp_payload, "success", 0);
        cJSON_AddStringToObject(resp_payload, "message", "Invalid username or password length");
        send_json_response(client->sock, response);
        cJSON_Delete(response);
        return;
    }

    /* Serialize registration request for Python backend */
    char req_json[512];
    snprintf(req_json, sizeof(req_json), "{\"username\":\"%s\",\"password\":\"%s\"}", 
             username->valuestring, password->valuestring);

    char resp_json[1024];
    int success = 0;
    int user_id = -1;
    char error_msg[256] = "Registration failed";

    if (http_post_json("/register", req_json, resp_json, sizeof(resp_json)) == 0) {
        cJSON *parsed_resp = cJSON_Parse(resp_json);
        if (parsed_resp) {
            cJSON *succ = cJSON_GetObjectItem(parsed_resp, "success");
            if (succ && (succ->type == cJSON_True || succ->valueint == 1)) {
                cJSON *uid = cJSON_GetObjectItem(parsed_resp, "user_id");
                if (uid && uid->type == cJSON_Number) {
                    success = 1;
                    user_id = uid->valueint;
                }
            } else {
                cJSON *msg = cJSON_GetObjectItem(parsed_resp, "message");
                if (msg && msg->type == cJSON_String) {
                    strncpy(error_msg, msg->valuestring, sizeof(error_msg) - 1);
                }
            }
            cJSON_Delete(parsed_resp);
        }
    }

    if (success) {
        /* Auto-login after registration */
        client->authenticated = 1;
        client->user_id = user_id;
        strncpy(client->username, username->valuestring, MAX_USERNAME - 1);
        client->username[MAX_USERNAME - 1] = '\0';

        cJSON_AddBoolToObject(resp_payload, "success", 1);
        cJSON_AddStringToObject(resp_payload, "message", "Registration successful");
        cJSON_AddNumberToObject(resp_payload, "user_id", user_id);
        cJSON_AddStringToObject(resp_payload, "username", client->username);

        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "New user registered: %s (id=%d)", client->username, user_id);
        log_event("INFO", log_msg);
        printf("[REG] %s\n", log_msg);
    } else {
        cJSON_AddBoolToObject(resp_payload, "success", 0);
        cJSON_AddStringToObject(resp_payload, "message", error_msg);
        printf("[REG] Registration failed for '%s'\n", username->valuestring);
    }

    send_json_response(client->sock, response);
    cJSON_Delete(response);
}

/*
 * handle_location: Process a location update message.
 */
static void handle_location(ClientInfo *client, cJSON *payload) {
    cJSON *latitude = cJSON_GetObjectItem(payload, "latitude");
    cJSON *longitude = cJSON_GetObjectItem(payload, "longitude");

    if (!latitude || !longitude || latitude->type != cJSON_Number || longitude->type != cJSON_Number) {
        return;
    }

    /* Send location to Python backend to store in DB */
    char req_json[256];
    snprintf(req_json, sizeof(req_json), "{\"user_id\":%d,\"latitude\":%f,\"longitude\":%f}", 
             client->user_id, latitude->valuedouble, longitude->valuedouble);

    /* We don't need to block waiting for response handling or errors for stats */
    http_post_json("/location", req_json, NULL, 0);
}

/*
 * handle_message: Main message dispatcher.
 */
void handle_message(ClientInfo *client_info, const char *message) {
    cJSON *json = cJSON_Parse(message);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "[PROTO] JSON parse error near: %s\n", error_ptr);
        }
        return;
    }

    cJSON *type = cJSON_GetObjectItem(json, "type");
    if (!type || type->type != cJSON_String || type->valuestring == NULL) {
        cJSON_Delete(json);
        return;
    }

    cJSON *payload = cJSON_GetObjectItem(json, "payload");

    /* Authentication and registration don't require prior auth */
    if (strcmp(type->valuestring, MSG_TYPE_AUTH) == 0) {
        handle_auth(client_info, payload);
    } else if (strcmp(type->valuestring, MSG_TYPE_REGISTER) == 0) {
        handle_register(client_info, payload);
    } else if (!client_info->authenticated) {
        /* Reject all other messages from unauthenticated clients */
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "type", MSG_TYPE_ERROR);
        cJSON *err_payload = cJSON_CreateObject();
        cJSON_AddItemToObject(error, "payload", err_payload);
        cJSON_AddStringToObject(err_payload, "message", "Authentication required");
        send_json_response(client_info->sock, error);
        cJSON_Delete(error);

        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "Unauthorized message attempt (type: %s)", type->valuestring);
        log_event("WARN", log_msg);
    } else if (strcmp(type->valuestring, MSG_TYPE_LOCATION) == 0) {
        handle_location(client_info, payload);
    } else if (strcmp(type->valuestring, MSG_TYPE_SUBSCRIBE) == 0) {
        cJSON *channel = cJSON_GetObjectItem(payload, "channel");
        if (channel && channel->type == cJSON_String) {
            printf("[SUB] %s subscribed to: %s\n", client_info->username, channel->valuestring);
        }
    } else {
        printf("[PROTO] Unknown message type: %s\n", type->valuestring);
    }

    cJSON_Delete(json);
}

/*
 * log_event: Send a log event to the Python backend.
 */
void log_event(const char *level, const char *message) {
    char req_json[512];
    /* Manually escape JSON double quotes strictly if needed, assumes simple message strings here */
    snprintf(req_json, sizeof(req_json), "{\"level\":\"%s\",\"message\":\"%s\"}", level, message);
    http_post_json("/log", req_json, NULL, 0);
}
