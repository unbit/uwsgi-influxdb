#include <uwsgi.h>
#include <curl/curl.h>
#include <time.h>

/*

This is a stats pusher plugin for the influxdb server.
It exports values exposed by the metric subsystem.
The following command-line argument can be used to configure the plug.

--stats-push influxdb:http://<username>:<password>@<host>:<port>/write?db=<dbname>,<tags (tag1=1,tag2=2,...)>

Examples:

--stats-push influxdb:http://localhost:8086/write?db=uwsgi,region=us-west,direction=in
--stats-push influxdb:http://myuser:12345@localhost:8086/write?db=uwsgi,region=us-west,direction=in
--stats-push influxdb:http://myuser:12345@localhost:8086/write?db=uwsgi

*/

extern struct uwsgi_server uwsgi;

struct uspi_args {
    char *url;
    char *tags;
};

/*

Send metric to influxdb via curl

Example request body:
'cpu_load_short,direction=in,host=server01,region=us-west value=2.0 1422568543702900257'

*/
static void influxdb_send_metric(struct uwsgi_buffer *ub, struct uspi_args *args, char *metric_name, size_t metric_len, int64_t value) {
	// reset the buffer
	ub->pos = 0;

	if (uwsgi_buffer_append(ub, "uwsgi ",5)) goto error;
    if (strlen(args->tags)) {
        if (uwsgi_buffer_append(ub, ",", 1)) goto error;
        if (uwsgi_buffer_append(ub, args->tags, strlen(args->tags))) goto error;
	if (uwsgi_buffer_append(ub, " ", 1)) goto error;
    }
	if (uwsgi_buffer_append(ub, metric_name, metric_len)) goto error;
	if (uwsgi_buffer_append(ub, "=", 1)) goto error;
    if (uwsgi_buffer_num64(ub, value)) goto error;
	if (uwsgi_buffer_append(ub, " ", 1)) goto error;

        unsigned long now = (unsigned long) time(NULL);
	char buf[20 + 1]; // 20 for unsigned long, 1 for \0
	snprintf(buf, 21, "%llu000000000\0", now);  // convert to nanoseconds and string
	if (uwsgi_buffer_append(ub, buf, 21)) goto error;

	// now send the body to the influxdb server via curl
	CURL *curl = curl_easy_init();
	if (!curl) {
		uwsgi_log_verbose("[influxdb] unable to initialize curl for metric %.*s\n", metric_len, metric_name);
		return;
	}
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, uwsgi.socket_timeout);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, uwsgi.socket_timeout);
	curl_easy_setopt(curl, CURLOPT_URL, args->url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, ub->buf);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		uwsgi_log_verbose("[influxdb] error sending metric %.*s: %s\n", metric_len, metric_name, curl_easy_strerror(res));
		curl_easy_cleanup(curl);
		return;
	}
	long http_code = 0;
#ifdef CURLINFO_RESPONSE_CODE
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
#else
	curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &http_code);
#endif
	if (http_code != 204) {
		uwsgi_log_verbose("[influxdb] HTTP api returned non-200 response code for %.*s: %d\n", metric_len, metric_name, (int) http_code);
	}
	curl_easy_cleanup(curl);
	return;
error:
	uwsgi_log_verbose("[influxdb] unable to generate body for %.*s\n", metric_len, metric_name);
}


/*

Parse plugin arg for paramters.

*/
static void parse_uspi_arg(char *plugin_arg, struct uspi_args *args) {
        int url_length;
        char *pos = strchr(plugin_arg, ',');
        if (pos == NULL) {
            url_length = strlen(plugin_arg);
        } else {
            url_length = (int)(pos - plugin_arg);
        }

        args->url = (char *) malloc(url_length + 1);
        strncpy(args->url, plugin_arg, url_length);
        args->url[url_length] = '\0';  // safety first

        int tags_length = strlen(plugin_arg) - url_length;
        args->tags = (char *) malloc(tags_length + 1);
        if (tags_length) {
            strncpy(args->tags, plugin_arg + url_length + 1, tags_length);
        }
        args->tags[tags_length] = '\0';  // safety first
}


/*

Free uspi_args struct

*/
void free_uspi_args(struct uspi_args *args) {
        free(args->url);
        free(args->tags);
        free(args);
}


/*

Collect stats from uWSGI and operate on each.

*/
static void stats_pusher_influxdb(struct uwsgi_stats_pusher_instance *uspi, time_t now, char *json, size_t json_len) {
	// we use the same buffer for all of the metrics
	struct uwsgi_buffer *ub = uwsgi_buffer_new(uwsgi.page_size);
	struct uwsgi_metric *um = uwsgi.metrics;
	struct uspi_args *args = malloc(sizeof(struct uspi_args));
	parse_uspi_arg(uspi->arg, args);

	while(um) {
		uwsgi_rlock(uwsgi.metrics_lock);
		int64_t value = *um->value;
		uwsgi_rwunlock(uwsgi.metrics_lock);

        influxdb_send_metric(ub, args, um->name, um->name_len, value);

		if (um->reset_after_push){
			uwsgi_wlock(uwsgi.metrics_lock);
			*um->value = um->initial_value;
			uwsgi_rwunlock(uwsgi.metrics_lock);
		}

		um = um->next;
	}

	free_uspi_args(args);

	uwsgi_buffer_destroy(ub);
}


/*

Initialize the plugin.

*/
static void influxdb_init(void) {
        struct uwsgi_stats_pusher *usp = uwsgi_register_stats_pusher("influxdb", stats_pusher_influxdb);
	// we use a custom format not the JSON one
	usp->raw = 1;
}


/*

Declare the plugin to the uWSIG plugin system.

*/
struct uwsgi_plugin influxdb_plugin = {
        .name = "influxdb",
        .on_load = influxdb_init,
};
