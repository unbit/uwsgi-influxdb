#include <uwsgi.h>
#include <curl/curl.h>

/*

this is a stats pusher plugin for the influxdb server:

--stats-push influxdbdb:http://<host>:<port>/db/<dbname/series?u=<user>&p=<pass>

it exports values exposed by the metric subsystem

*/

extern struct uwsgi_server uwsgi;

/*
JSON body:

[{"name":"NAME","columns":["value"],"points":[[VALUE]]}]\0
*/
static void influxdb_send_metric(struct uwsgi_buffer *ub, char *url, char *metric, size_t metric_len, int64_t value) {
	// reset the buffer
	ub->pos = 0;
	if (uwsgi_buffer_append(ub, "[{\"name\":\"", 10)) goto error;	
	if (uwsgi_buffer_append_json(ub, metric, metric_len)) goto error;
	if (uwsgi_buffer_append(ub, "\",\"columns\":[\"value\"],\"points\":[[", 33)) goto error;
        if (uwsgi_buffer_num64(ub, value)) goto error;
	if (uwsgi_buffer_append(ub, "]]}]\0", 5)) goto error;

	// now send the JSON to the influxdb server via curl
	CURL *curl = curl_easy_init();
	if (!curl) {
		uwsgi_log_verbose("[influxdb] unable to initialize curl for metric %.*s\n", metric_len, metric);
		return;
	}
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, uwsgi.socket_timeout);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, uwsgi.socket_timeout);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); 
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, ub->buf);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	CURLcode res = curl_easy_perform(curl);	
	curl_slist_free_all(headers);
	if (res != CURLE_OK) {
		uwsgi_log_verbose("[influxdb] error sending metric %.*s: %s\n", metric_len, metric, curl_easy_strerror(res));	
		curl_easy_cleanup(curl);
		return;
	}
	long http_code = 0;
#ifdef CURLINFO_RESPONSE_CODE
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
#else
	curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &http_code);
#endif
	if (http_code != 200) {
		uwsgi_log_verbose("[influxdb] HTTP api returned non-200 response code for %.*s: %d\n", metric_len, metric, (int) http_code);
	}
	curl_easy_cleanup(curl);
	return;
error:
	uwsgi_log_verbose("[influxdb] unable to generate JSON for %.*s\n", metric_len, metric);
}


static void stats_pusher_influxdb(struct uwsgi_stats_pusher_instance *uspi, time_t now, char *json, size_t json_len) {

	// we use the same buffer for all of the metrics
	struct uwsgi_buffer *ub = uwsgi_buffer_new(uwsgi.page_size);
	struct uwsgi_metric *um = uwsgi.metrics;
	while(um) {
		uwsgi_rlock(uwsgi.metrics_lock);
		int64_t value = *um->value;
		uwsgi_rwunlock(uwsgi.metrics_lock);
		influxdb_send_metric(ub, uspi->arg, um->name, um->name_len, value);
		if (um->reset_after_push){
			uwsgi_wlock(uwsgi.metrics_lock);
			*um->value = um->initial_value;
			uwsgi_rwunlock(uwsgi.metrics_lock);
		}
		um = um->next;
	}

	uwsgi_buffer_destroy(ub);
}

static void influxdb_init(void) {
        struct uwsgi_stats_pusher *usp = uwsgi_register_stats_pusher("influxdb", stats_pusher_influxdb);
	// we use a custom format not the JSON one
	usp->raw = 1;
}

struct uwsgi_plugin influxdb_plugin = {
        .name = "influxdb",
        .on_load = influxdb_init,
};
