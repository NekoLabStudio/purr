#pragma once

// One HTTP request, made on the worker thread pool so the game never waits on the network. The
// result reaches the callback on the main thread, as a Dictionary:
//   {"status": HTTP status (0 if it never got an answer), "headers": {lowercase name: value},
//    "body": PackedByteArray, "error": "" or why it failed}

#include "core/io/http_client.h"
#include "core/variant/callable.h"

void purr_http_request(HTTPClient::Method p_method, const String &p_url, const Vector<String> &p_headers, const PackedByteArray &p_body, const Callable &p_done);
