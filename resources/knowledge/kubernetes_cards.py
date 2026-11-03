"""Original Kubernetes and K9s reading cards; no cluster commands execute here."""
from card import make


def cards():
    out = []
    kube = 'https://kubernetes.io/docs/reference/kubectl/quick-reference/'
    k9s = 'https://k9scli.io/topics/commands/'
    version = '2026-10-30核对官方文档；大写名称为待替换值；按本机帮助核对版本'

    def add(tool, slug, title, overview, code, note, source=None):
        out.append(make('tool.' + tool + '.' + slug, title, '工具',
            'Kubernetes' if tool == 'kubectl' else 'K9s', overview, code, note,
            source or (kube if tool == 'kubectl' else k9s), version,
            aliases=[tool, 'k8s' if tool == 'kubectl' else 'k9s', slug],
            language='shell'))

    add('kubectl', 'help', 'K8s入门与占位符',
        'kubectl是集群命令行工具。NS为命名空间，CTX为上下文，POD为实例名，CONTAINER为容器名，DEPLOY为部署名。先替换再使用。', '''
kubectl version --client
kubectl --help
kubectl logs --help
''', '所有示例仅供阅读。命令在电脑终端执行；设备不连接或操作集群。客户端与服务端版本需兼容。')
    add('kubectl', 'context', '上下文与集群切换',
        '上下文包含集群、身份与默认命名空间。先检查当前目标，再切换到CTX。', '''
kubectl config get-contexts
kubectl config current-context
kubectl config use-context CTX
''', 'use-context修改本地默认目标。临时查询可用kubectl --context CTX get pods -n NS，不改变默认上下文。')
    add('kubectl', 'namespace', '命名空间选择',
        '-n NS只作用于当前命令；set-context保存当前上下文的默认命名空间。', '''
kubectl get namespaces
kubectl get pods -n NS
kubectl config set-context --current --namespace=NS
''', '-A查看所有命名空间，需要相应权限。Node等集群级资源不受-n限制。')
    add('kubectl', 'resources', '资源查询与持续观察',
        'get列出资源；-o wide增加节点和地址等信息；-w跟踪后续变化。', '''
kubectl get pods -n NS -o wide
kubectl get deploy,svc -n NS
kubectl get pods -n NS -w
kubectl get nodes
''', 'Ctrl+C结束跟踪。get all只涵盖部分资源类型；用api-resources查看完整资源类型。')
    add('kubectl', 'filter', '标签与状态过滤',
        '-l按标签筛选；field-selector按支持的字段筛选。APP是应用标签值。', '''
kubectl get pods -n NS --show-labels
kubectl get pods -n NS -l app=APP
kubectl get pods -n NS --field-selector=status.phase=Running
''', 'Running是Pod阶段，不保证Ready或业务健康。不同资源支持的字段选择器不同。')
    add('kubectl', 'output', 'YAML与JSON输出',
        '查看资源的原始字段或仅输出名称，适合核对实际配置。', '''
kubectl get pod POD -n NS -o yaml
kubectl get deployment DEPLOY -n NS -o json
kubectl get pods -n NS -o name
''', '输出可能包含环境变量和业务配置；分享前检查敏感内容。不要直接把服务端字段完整复制为新配置。')
    add('kubectl', 'describe', '资源详情与故障定位',
        'describe汇总状态、容器、探针和关联事件。先看状态，再看事件原因。', '''
kubectl describe pod POD -n NS
kubectl describe deployment DEPLOY -n NS
kubectl describe node NODE
''', 'NODE替换为节点名。Pending重点看调度事件，重启重点看上次退出状态、探针和容器日志。')
    add('kubectl', 'events', '事件与警告查询',
        '按事件创建时间排序，查找调度、拉取镜像和探针问题。', '''
kubectl get events -n NS --sort-by=.metadata.creationTimestamp
kubectl events -n NS --types=Warning
''', '事件会过期，不是永久审计记录。较旧kubectl没有events子命令时用get events。')
    add('kubectl', 'logs', '查看与跟踪容器日志',
        '-c选择容器，--tail限制行数，--since限制时间范围；-f持续跟踪。', '''
kubectl logs POD -n NS -c CONTAINER --tail=100
kubectl logs POD -n NS -c CONTAINER --since=10m
kubectl logs POD -n NS -c CONTAINER -f
''', 'Ctrl+C结束跟踪。日志来自容器标准输出和错误输出，不会自动读取容器内任意日志文件。')
    add('kubectl', 'previous', '重启前日志与多容器',
        '--previous读取同一Pod中上一次终止的容器日志；--all-containers合并当前容器输出。', '''
kubectl logs POD -n NS -c CONTAINER --previous
kubectl logs POD -n NS --all-containers=true --prefix=true --tail=100
''', '上一个容器日志可能已清理；Pod被替换后，新Pod不能用previous找回旧Pod日志。')
    add('kubectl', 'exec', '进入容器与执行命令',
        '--分隔kubectl参数与容器内命令；-i保留输入，-t申请终端。', '''
kubectl exec POD -n NS -c CONTAINER -- ls /
kubectl exec -it POD -n NS -c CONTAINER -- /bin/sh
''', '镜像必须包含指定命令或shell，并且用户有exec权限。输入exit退出容器；容器内修改可能随重建丢失。')
    add('kubectl', 'apply', '配置预览与应用',
        'FILE.yaml是本地资源文件。先预览差异，再创建或更新文件声明的资源。', '''
kubectl apply -n NS --dry-run=client -f FILE.yaml
kubectl diff -n NS -f FILE.yaml
kubectl apply -n NS -f FILE.yaml
''', '最后一条会修改集群。client预演不保证服务端接受；diff退出码1表示存在差异，文件目标需与NS一致。')
    add('kubectl', 'rollout', '部署进度与回滚',
        '先查看发布进度与历史，再决定是否回到上一版本。', '''
kubectl rollout status deployment/DEPLOY -n NS --timeout=120s
kubectl rollout history deployment/DEPLOY -n NS
kubectl rollout undo deployment/DEPLOY -n NS
''', 'undo会修改运行中的部署；依赖保留的历史版本，不能回滚外部数据库或配置。超时不代表发布自动取消。')
    add('kubectl', 'scale', '副本数与滚动重启',
        'scale设置目标副本数；rollout restart触发部署的滚动重启。', '''
kubectl scale deployment/DEPLOY -n NS --replicas=3
kubectl rollout restart deployment/DEPLOY -n NS
kubectl rollout status deployment/DEPLOY -n NS
''', '这两项会改变运行状态。HPA可能重新调整副本数；滚动重启仍需健康检查和可用容量。')
    add('kubectl', 'port-forward', '本地端口转发',
        '把本机8080转发到目标Pod的80端口，或Service的80端口。SVC为服务名。', '''
kubectl port-forward -n NS pod/POD 8080:80
kubectl port-forward -n NS service/SVC 8080:80
''', '两条选一，按实际端口替换。默认只监听本机；保持终端运行，Ctrl+C停止。目标Pod终止后需要重连。')
    add('kubectl', 'top', 'CPU与内存观察',
        'top展示近期资源使用，--containers细分到容器。', '''
kubectl top nodes
kubectl top pods -n NS
kubectl top pod POD -n NS --containers
''', '需要可用的资源指标服务，例如Metrics Server。采样不是完整历史，也不能替代监控告警。')
    add('kubectl', 'auth', '权限检查',
        'auth can-i询问当前身份是否允许指定操作，适合排查Forbidden。', '''
kubectl auth can-i get pods -n NS
kubectl auth can-i get pods --subresource=log -n NS
kubectl auth can-i create pods --subresource=exec -n NS
''', '权限允许不代表资源存在或网络正常。不要通过更换高权限身份绕过未确认的访问边界。',
        'https://kubernetes.io/docs/reference/kubectl/generated/kubectl_auth/kubectl_auth_can-i/')
    add('kubectl', 'explain', '资源类型与字段帮助',
        'api-resources查资源名和缩写；explain查看字段说明。', '''
kubectl api-resources
kubectl explain deployment.spec
kubectl explain pod.spec.containers
''', '以目标集群提供的API为准；自定义资源是否提供完整字段说明取决于它的结构定义。')

    add('k9s', 'help', 'K9s帮助与运行信息',
        'K9s在电脑终端里浏览和管理集群。启动后按?查看当前页面快捷键；Ctrl+A查看资源别名。', '''
k9s help
k9s info
''', '命令在终端输入；后续冒号、斜线和单键示例在K9s界面操作。快捷键以当前视图帮助为准。')
    add('k9s', 'launch', '启动到指定集群',
        'CTX为上下文，NS为命名空间；-c指定初始资源视图。', '''
k9s --context CTX -n NS
k9s -n NS -c pod
''', '需要有效kubeconfig、网络连接和相应权限。先核对界面显示的集群与命名空间。')
    add('k9s', 'readonly', '只读浏览模式',
        '--readonly禁用K9s中的修改命令，适合日常查看状态。', '''
k9s --readonly --context CTX -n NS
''', '这是客户端操作限制，不改变集群权限；真正的只读权限应由集群RBAC控制。')
    add('k9s', 'resources', '切换资源页面',
        '按冒号进入命令栏，输入资源名或缩写后回车。', '''
:pods
:deploy
:svc
:nodes
''', '支持单数、复数和资源别名。输入命令后按Enter；Esc退出命令栏或返回上一视图。')
    add('k9s', 'namespace', '切换命名空间',
        ':ns打开命名空间列表，选择后回车；也可直接进入指定空间的Pod列表。', '''
:ns
:pod NS
''', 'NS替换为命名空间名称。切换后检查顶部作用域，避免把不同空间的同名资源混淆。')
    add('k9s', 'context', '切换集群上下文',
        ':ctx列出上下文供选择；指定名称可直接切换。', '''
:ctx
:ctx CTX
:pod @CTX
''', 'CTX替换为实际上下文名。@CTX也会切换当前K9s上下文，确认目标后再执行资源操作。')
    add('k9s', 'filter', '名称过滤与模糊查找',
        '斜线开启过滤；普通表达式匹配名称，!反向过滤，-f启用模糊查找。', '''
/api|worker
/!test
/-f api
''', '示例词替换为需要查找的内容，回车确认；Esc退出过滤。过滤只影响当前列表展示。')
    add('k9s', 'labels', '按标签查找资源',
        '-l使用标签选择器；也可在资源命令后直接指定标签。', '''
/-l app=APP
:pod app=APP,env=ENV
''', 'APP和ENV是标签值。多个逗号分隔条件需要同时满足；标签匹配与名称模糊查找不同。')
    add('k9s', 'detail', '查看详情与YAML',
        '在资源列表选中一行，按d查看describe详情，按v查看资源内容，按?查看当前可用按键。', '''
d
v
?
''', '以上每行是独立按键，不是终端命令。e会进入编辑并可能修改资源，不是只读查看。')
    add('k9s', 'logs', '查看Pod与容器日志',
        '进入Pod列表，选择目标后按l查看日志；多容器时按界面选择容器。', '''
:pods
l
?
''', '在日志视图按?查看当前版本的跟随、过滤等快捷键；Esc返回。日志能否读取受容器状态和权限限制。')
    add('k9s', 'shell', '进入容器Shell',
        '在Pod或容器列表选择目标，按s发起shell会话；结束时在shell中输入exit。', '''
:pods
s
''', '目标镜像需提供shell且当前身份允许exec。只读模式可能禁用该操作；当前可用操作以?帮助为准。',
        'https://github.com/derailed/k9s')
    add('k9s', 'exit', '返回与退出操作',
        'Esc退出当前视图、命令或过滤；:q与Ctrl+C退出K9s。', '''
:q
''', '退出K9s不停止集群业务。容器shell中先输入exit结束会话，再回到K9s操作。')
    return out
