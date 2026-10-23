"""Small operational examples for every concrete component card.

Endpoints and credentials are placeholders. Version-dependent commands stay
explicitly scoped; distributed concepts do not pretend to be shell commands.
"""
OPERATIONS={
'rabbitmq':('RabbitMQ CLI；已安装并启动本机节点',r'''rabbitmq-diagnostics ping
rabbitmqctl list_vhosts
rabbitmqctl list_queues -p / name messages consumers
rabbitmqctl list_connections''','CLI账号需能访问节点的Erlang cookie。发布和消费还需交换机、队列绑定与确认策略；不得随意purge业务队列。'),
'kafka':('Apache Kafka发行包CLI；本机演示broker',r'''bin/kafka-topics.sh --bootstrap-server localhost:9092 --list
bin/kafka-topics.sh --bootstrap-server localhost:9092 --create --topic demo --partitions 1 --replication-factor 1
bin/kafka-console-producer.sh --bootstrap-server localhost:9092 --topic demo
bin/kafka-console-consumer.sh --bootstrap-server localhost:9092 --topic demo --from-beginning''','生产者启动后输入消息行，Ctrl+C停止。副本1仅演示；认证集群需配置客户端凭据。'),
'rocketmq':('RocketMQ mqadmin；示例NameServer',r'''sh bin/mqadmin clusterList -n localhost:9876
sh bin/mqadmin topicList -n localhost:9876
sh bin/mqadmin topicRoute -n localhost:9876 -t DemoTopic
sh bin/mqadmin consumerProgress -n localhost:9876 -g DemoGroup''','替换已存在的主题和消费组。生产消费请用对应版本客户端；不要在业务组上练习重置消费位点。'),
'pulsar':('Apache Pulsar发行包CLI；已配置本地服务',r'''bin/pulsar-admin topics list public/default
bin/pulsar-client produce persistent://public/default/demo -m hello
bin/pulsar-client consume persistent://public/default/demo -s demo-reader -p Earliest -n 1''','订阅名决定消费进度；Earliest仅对新订阅起点有效。认证和服务地址以客户端配置为准。'),
'mysql':('MySQL 8.4；在mysql客户端执行',r'''SHOW DATABASES;
USE demo;
SHOW TABLES;
SELECT id,name FROM users ORDER BY id LIMIT 10;
SHOW CREATE TABLE users;''','users使用本知识包MySQL创建示例表。增删改查、索引与事务详见MySQL分类。'),
'postgresql':('PostgreSQL psql；连接到演示库',r'''psql -h localhost -U demo -d demo
# In psql:
# \dt
# SELECT id,name FROM users ORDER BY id LIMIT 10;
# \d users
# \q''','井号行说明进入psql后应执行的命令，输入时去掉注释前缀。users需预先创建；认证按服务配置。'),
'sqlite':('SQLite sqlite3 CLI；独立演示文件',r'''sqlite3 demo.db
# In sqlite3:
# CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY,name TEXT);
# INSERT INTO users(name) VALUES('Ada');
# SELECT * FROM users;
# .tables
# .schema users
# .quit''','井号后的内容在sqlite3交互终端执行。打开不存在的路径会新建数据库，先确认工作目录。'),
'redis':('Redis redis-cli；已连接演示实例',r'''redis-cli PING
redis-cli SET demo:name Ada EX 60
redis-cli GET demo:name
redis-cli TTL demo:name
redis-cli HSET demo:user name Ada score 90
redis-cli HGETALL demo:user
redis-cli DEL demo:name demo:user''','示例会写入并删除demo键。大库遍历用SCAN游标而非KEYS *；密码优先由安全配置提供。'),
'memcached':('Memcached基础文本协议；通过客户端连接后发送',r'''set demo 0 60 5
hello
get demo
delete demo
stats
quit''','协议行以CRLF结束，5是值hello的字节数。不是Shell命令；仅对受控实例操作，不对公网裸露服务。'),
'mongodb':('MongoDB mongosh；进入隔离demo库',r'''use demo
db.users.insertOne({name:"Ada",score:90})
db.users.find({score:{$gte:80}}).limit(10)
db.users.updateOne({name:"Ada"},{$set:{score:95}})
db.users.deleteOne({name:"Ada"})''','这是mongosh交互语法。查询条件可能匹配多条，生产更新应使用唯一_id及合适条件。'),
'elasticsearch':('Elasticsearch HTTP API；Kibana Dev Tools格式',r'''GET /_cluster/health
PUT /demo
PUT /demo/_doc/1
{"name":"Ada"}
GET /demo/_search
{"query":{"match":{"name":"Ada"}}}
DELETE /demo/_doc/1''','这是Dev Tools请求格式，不是Shell。默认安全部署需要HTTPS与凭据；索引映射应在生产入库前定义。'),
'opensearch':('OpenSearch HTTP API；Dashboards Dev Tools格式',r'''GET /_cluster/health
PUT /demo
PUT /demo/_doc/1
{"name":"Ada"}
GET /demo/_search
{"query":{"match_all":{}},"size":10}
DELETE /demo/_doc/1''','使用隔离的demo索引，确认目标集群、认证和映射；删除文档不等于立即从磁盘移除数据。'),
'clickhouse':('ClickHouse SQL；clickhouse-client内执行',r'''SHOW DATABASES;
CREATE DATABASE IF NOT EXISTS demo;
CREATE TABLE IF NOT EXISTS demo.events(id UInt64,name String)
ENGINE=MergeTree ORDER BY id;
INSERT INTO demo.events VALUES(1,'Ada');
SELECT name,count() FROM demo.events GROUP BY name;''','MergeTree排序键不保证id唯一；小批频繁插入增加parts，正式采集应批量写入。'),
'doris':('Apache Doris SQL；MySQL协议客户端内执行',r'''SHOW DATABASES;
USE demo;
SHOW TABLES;
DESC events;
SELECT event_type,COUNT(*) FROM events GROUP BY event_type;
SHOW LOAD FROM demo;''','假定已有demo.events及event_type列。建表模型、分桶和副本数需结合集群，不套用MySQL建表参数。'),
'influxdb':('InfluxDB 2.x influx CLI；已配置演示组织和token',r'''influx bucket list --org demo
influx write --org demo --bucket sensors 'temperature,room=office value=23.5'
influx query --org demo 'from(bucket:"sensors") |> range(start:-1h) |> limit(n:10)' ''','仅适用于2.x的bucket和Flux工作流，1.x和3.x不可直接照搬；bucket应预先创建。'),
'neo4j':('Neo4j Cypher；Browser或cypher-shell内执行',r'''CREATE (:DemoPerson {name:'Ada'});
MATCH (p:DemoPerson) RETURN p LIMIT 10;
MATCH (p:DemoPerson {name:'Ada'}) SET p.score=90;
MATCH (p:DemoPerson {name:'Ada'}) DETACH DELETE p;''','最后一句会删除匹配节点及其关系，仅用于隔离演示标签；正式数据先核对匹配范围。'),
'nacos':('Nacos 2.x Open API；单机未启用认证的本地演示',r'''curl -G 'http://127.0.0.1:8848/nacos/v1/cs/configs' --data-urlencode 'dataId=demo.properties' --data-urlencode 'group=DEFAULT_GROUP'
curl -G 'http://127.0.0.1:8848/nacos/v1/ns/instance/list' --data-urlencode 'serviceName=demo' ''','此处只读取配置与实例。启用认证时需要相应token；Nacos 3.x按其版本API另行调整，不关闭认证求兼容。'),
'zookeeper':('ZooKeeper zkCli.sh内执行',r'''ls /
create /demo hello
get /demo
set /demo world
get -s /demo
delete /demo''','先用发行包bin/zkCli.sh -server host:2181连接。不要操作业务节点；需要配置ACL，默认开放ACL不适合生产。'),
'etcd':('etcd v3 etcdctl；已配置端点及TLS认证',r'''etcdctl endpoint health
etcdctl put /demo/name Ada
etcdctl get /demo/ --prefix
etcdctl watch /demo/ --prefix
# In another terminal:
etcdctl del /demo/name''','watch持续等待，用Ctrl+C停止；删除仅限示例键，不要对控制面根前缀批量操作。'),
'nginx':('Nginx已安装；命令需对应权限与配置路径',r'''nginx -v
nginx -t
nginx -s reload''','只有-t通过后再reload。使用systemd托管时也可systemctl reload nginx；不要启动第二个争抢端口的实例。'),
'gateway':('Spring Cloud Gateway；WebFlux配置流程',r'''# application.yml shape (property namespace depends on release):
# routes:
#   - id: demo
#     uri: http://localhost:8081
#     predicates:
#       - Path=/demo/**
curl -i --max-time 10 http://localhost:8080/demo/health''','先匹配Spring Boot和Cloud版本，按该版本的路由属性前缀配置、重启、请求验证。示例保留/demo路径，后端需有对应端点。'),
'docker':('Docker Engine CLI；存在名为demo的容器时',r'''docker version
docker images
docker ps -a
docker logs --tail 100 demo
docker inspect demo
docker stats --no-stream
docker stop demo''','创建示例：docker run --name demo -d -p 8080:80 nginx:alpine。此命令会拉镜像并启动服务；正式部署固定版本和持久卷。'),
'kubernetes':('kubectl；已配置演示集群和命名空间',r'''kubectl config current-context
kubectl get pods -n demo
kubectl get services -n demo
kubectl describe pod demo-pod -n demo
kubectl logs demo-pod -n demo --tail=100
kubectl apply -f deployment.yaml
kubectl rollout status deployment/demo -n demo''','核对context和namespace后再apply；demo-pod与部署名为占位。多容器Pod日志可用-c指定容器。'),
}

def enrich(cards):
    from card import section
    for card in cards:
        key=card['id'].removeprefix('component.')
        if key not in OPERATIONS:continue
        version,code,note=OPERATIONS[key]
        card['sections'][1:1]=[
            section('operation-context','操作环境',version+'。'+note),
            section('example','示例片段',code,True,'shell')]
    return cards
