uwsgi-influxdb
==============

uWSGI plugin for influxdb integration

INSTALL
=======

The plugin is 2.x friendly:

```sh
uwsgi --build-plugin https://github.com/unbit/uwsgi-influxdb
```

USAGE
=====

Just pass the url of your influxdb api:

```ini
[uwsgi]
master = true
processes = 8
threads = 4

http-socket = :9090
enable-metrics = true

plugin = influxdb
stats-push = influxdb:http://localhost:8086/db/uwsgi/series?u=root&p=root
```
