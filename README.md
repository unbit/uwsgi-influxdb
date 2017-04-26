uwsgi-influxdb
==============

uWSGI plugin for influxdb integration

INSTALL
=======

The plugin is uWSGI 2.x and Influx 1.0+ friendly:

```sh
uwsgi --build-plugin https://github.com/unbit/uwsgi-influxdb
```

USAGE
=====

Just pass the url of your influxdb api and tags:

General:

```
--stats-push influxdb:http://<username>:<password>@<host>:<port>/write?db=<dbname>,<tags (tag1=1,tag2=2,...)>
```

Command Line:

```
--stats-push influxdb:http://myuser:12345@localhost:8086/write?db=uwsgi,region=us-west,direction=in
```


INI:

```ini
[uwsgi]
master = true
processes = 8
threads = 4

http-socket = :9090
enable-metrics = true

plugin = influxdb
stats-push =influxdb:http://host:8086/write?db=dbname&u=user&p=pass,tag=tag1

```


>>>>>>> cffed6f...  fix format values
