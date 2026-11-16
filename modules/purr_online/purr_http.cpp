#include "purr_http.h"

#include "core/crypto/crypto.h"
#include "core/object/callable_mp.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"

namespace {

// Gives up on a request taking longer than this.
constexpr uint64_t TIMEOUT_MSEC = 30000;

// A request in flight. It holds a reference to itself until it finishes on the main thread.
class PurrHttpTask : public RefCounted {
	GDSOFTCLASS(PurrHttpTask, RefCounted);

public:
	HTTPClient::Method method = HTTPClient::METHOD_GET;
	String url;
	Vector<String> headers;
	PackedByteArray body;
	Callable done;
	WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
	Ref<PurrHttpTask> self;

	// On the main thread: the pool keeps a finished task until it's waited for, so it's waited
	// for here (it returns at once), then the result goes to the caller.
	void finish(const Dictionary &p_result) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
		done.call(p_result);
		self.unref();
	}
};

Dictionary failed(const String &p_why) {
	Dictionary result;
	result["status"] = 0;
	result["headers"] = Dictionary();
	result["body"] = PackedByteArray();
	result["error"] = p_why;
	return result;
}

// Polls the client while its status is one of these, until the deadline; false if it ran out.
bool poll_while(const Ref<HTTPClient> &p_client, std::initializer_list<HTTPClient::Status> p_statuses, uint64_t p_deadline) {
	while (true) {
		bool waiting = false;
		for (HTTPClient::Status status : p_statuses) {
			waiting = waiting || p_client->get_status() == status;
		}
		if (!waiting) {
			return true;
		}
		if (OS::get_singleton()->get_ticks_msec() > p_deadline) {
			return false;
		}
		p_client->poll();
		OS::get_singleton()->delay_usec(1000);
	}
}

Dictionary perform(const PurrHttpTask &p_request) {
	String scheme;
	String host;
	int port = 0;
	String path;
	String fragment;
	if (p_request.url.parse_url(scheme, host, port, path, fragment) != OK || (scheme != "https://" && scheme != "http://")) {
		return failed("The service URL isn't a valid http(s) URL: " + p_request.url);
	}
	if (path.is_empty()) {
		path = "/";
	}

	Ref<HTTPClient> client = Ref<HTTPClient>(HTTPClient::create());
	const Ref<TLSOptions> tls = scheme == "https://" ? TLSOptions::client() : Ref<TLSOptions>();
	if (client->connect_to_host(host, port > 0 ? port : -1, tls) != OK) {
		return failed("Couldn't start connecting to " + host + ".");
	}
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + TIMEOUT_MSEC;
	if (!poll_while(client, { HTTPClient::STATUS_RESOLVING, HTTPClient::STATUS_CONNECTING }, deadline)) {
		return failed("Timed out connecting to " + host + ".");
	}
	if (client->get_status() != HTTPClient::STATUS_CONNECTED) {
		return failed("Couldn't connect to " + host + ".");
	}
	if (client->request(p_request.method, path, p_request.headers, p_request.body.ptr(), p_request.body.size()) != OK) {
		return failed("Couldn't send the request.");
	}
	if (!poll_while(client, { HTTPClient::STATUS_REQUESTING }, deadline)) {
		return failed("Timed out waiting for an answer.");
	}

	Dictionary headers;
	PackedByteArray body;
	if (client->has_response()) {
		List<String> lines;
		client->get_response_headers(&lines);
		for (const String &line : lines) {
			const int colon = line.find_char(':');
			if (colon > 0) {
				headers[line.substr(0, colon).strip_edges().to_lower()] = line.substr(colon + 1).strip_edges();
			}
		}
		while (client->get_status() == HTTPClient::STATUS_BODY) {
			if (OS::get_singleton()->get_ticks_msec() > deadline) {
				return failed("Timed out reading the answer.");
			}
			client->poll();
			const PackedByteArray chunk = client->read_response_body_chunk();
			if (chunk.is_empty()) {
				OS::get_singleton()->delay_usec(1000);
			} else {
				body.append_array(chunk);
			}
		}
	}

	Dictionary result;
	result["status"] = client->get_response_code();
	result["headers"] = headers;
	result["body"] = body;
	result["error"] = String();
	return result;
}

void run(void *p_userdata) {
	PurrHttpTask *request = static_cast<PurrHttpTask *>(p_userdata);
	callable_mp(request, &PurrHttpTask::finish).call_deferred(perform(*request));
}

} // namespace

void purr_http_request(HTTPClient::Method p_method, const String &p_url, const Vector<String> &p_headers, const PackedByteArray &p_body, const Callable &p_done) {
	Ref<PurrHttpTask> request;
	request.instantiate();
	request->method = p_method;
	request->url = p_url;
	request->headers = p_headers;
	request->body = p_body;
	request->done = p_done;
	request->self = request;
	// Set before finish() can run: that's deferred to the main thread, which is here.
	request->task = WorkerThreadPool::get_singleton()->add_native_task(&run, request.ptr(), false, "Purr HTTP request");
}
