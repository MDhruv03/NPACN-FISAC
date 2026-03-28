#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "http_client.h"

#define PYTHON_HOST "localhost"
#define PYTHON_PORT 5000

void handle_message(int client_sock, const char *message) {
    cJSON *json = cJSON_Parse(message);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }
        return;
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(json, "type");
    if (cJSON_IsString(type) && (type->valuestring != NULL)) {
        printf("Received message of type: %s\n", type->valuestring);

        if (strcmp(type->valuestring, MSG_TYPE_AUTH) == 0) {
            cJSON *payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
            cJSON *username = cJSON_GetObjectItemCaseSensitive(payload, "username");
            cJSON *password = cJSON_GetObjectItemCaseSensitive(payload, "password");
            if (cJSON_IsString(username) && cJSON_IsString(password)) {
                printf("Auth: user=%s, pass=%s\n", username->valuestring, password->valuestring);
                // Authentication logic will be implemented later
            }
        } else if (strcmp(type->valuestring, MSG_TYPE_LOCATION) == 0) {
            cJSON *payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
            cJSON *latitude = cJSON_GetObjectItemCaseSensitive(payload, "latitude");
            cJSON *longitude = cJSON_GetObjectItemCaseSensitive(payload, "longitude");
            if (cJSON_IsNumber(latitude) && cJSON_IsNumber(longitude)) {
                printf("Location: lat=%f, lon=%f\n", latitude->valuedouble, longitude->valuedouble);
                
                cJSON *location_data = cJSON_CreateObject();
                // In a real application, user_id would come from an authenticated session
                cJSON_AddNumberToObject(location_data, "user_id", 1); 
                cJSON_AddNumberToObject(location_data, "latitude", latitude->valuedouble);
                cJSON_AddNumberToObject(location_data, "longitude", longitude->valuedouble);
                
                char *json_str = cJSON_PrintUnformatted(location_data);
                http_post(PYTHON_HOST, PYTHON_PORT, "/location", json_str);
                free(json_str);
                cJSON_Delete(location_data);
            }
        } else if (strcmp(type->valuestring, MSG_TYPE_SUBSCRIBE) == 0) {
            cJSON *payload = cJSON_GetObjectItemCaseSensitive(json, "payload");
            cJSON *channel = cJSON_GetObjectItemCaseSensitive(payload, "channel");
            if (cJSON_IsString(channel)) {
                printf("Subscribe: channel=%s\n", channel->valuestring);
            }
        }
    }

    cJSON_Delete(json);
}

void log_event(const char* level, const char* message) {
    cJSON *log_data = cJSON_CreateObject();
    cJSON_AddStringToObject(log_data, "level", level);
    cJSON_AddStringToObject(log_data, "message", message);

    char *json_str = cJSON_PrintUnformatted(log_data);
    http_post(PYTHON_HOST, PYTHON_PORT, "/log", json_str);
    free(json_str);
    cJSON_Delete(log_data);
}
