/*
    http_client.h - Minimal HTTP POST client for WinSock2.

    Sends JSON payloads to localhost:5000 to interact with the Python backend.
*/

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <winsock2.h>

/* Perform an HTTP POST with JSON data to the local Flask server on port 5000.
   endpoint: e.g., "/auth", "/register"
   json_payload: the body of the POST request
   response_buffer: a buffer where the HTTP response body will be copied
   buffer_size: size of the response buffer
   Returns 0 on success, -1 on failure.
*/
int http_post_json(const char *endpoint, const char *json_payload, char *response_buffer, int buffer_size);

#endif /* HTTP_CLIENT_H */
