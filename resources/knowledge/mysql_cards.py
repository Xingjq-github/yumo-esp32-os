from card import table,MYSQL
def cards():
    out=[]
    def add(group,text):out.extend(table(text,'MySQL',group,MYSQL,'MySQL 8.4 / InnoDB；SQL片段，表字段按示例创建','mysql.','sql'))
    add('数据定义',r'''
create-database ~~ 创建数据库 ~~ CREATE DATABASE建立逻辑数据库，字符集与排序规则应明确选择并与客户端保持一致。 ~~ CREATE DATABASE demo CHARACTER SET utf8mb4;\nUSE demo; ~~ IF NOT EXISTS只避免同名错误，不验证已有库配置符合预期。
create-table ~~ 创建示例表 ~~ 本包查询示例通用表：users(id,name,dept,score,created_at)，先在测试库创建再运行查询。 ~~ CREATE TABLE users (\n  id BIGINT PRIMARY KEY, name VARCHAR(80), dept VARCHAR(40),\n  score INT, created_at DATETIME\n) ENGINE=InnoDB; ~~ 示例刻意精简，真实业务需要明确NOT NULL、默认值、索引和约束。
integer ~~ 选择整数类型 ~~ INT是4字节，BIGINT是8字节；有符号和UNSIGNED范围不同。按业务上界选型。 ~~ CREATE TABLE counter (id BIGINT PRIMARY KEY, amount INT UNSIGNED NOT NULL); ~~ UNSIGNED减法及类型转换可能改变结果或报错；不要只按当前数据量估算。
decimal ~~ 精确小数 ~~ DECIMAL(M,D)为精确十进制，M总位数、D小数位数，适合金额等需要可控精度的数据。 ~~ CREATE TABLE prices (id BIGINT PRIMARY KEY, amount DECIMAL(12,2)); ~~ FLOAT/DOUBLE为近似值；舍入和越界受SQL模式影响。
varchar ~~ 文本类型与字符集 ~~ VARCHAR长度以字符计，实际字节随字符集变化。utf8mb4可表示完整Unicode范围。 ~~ CREATE TABLE labels (id BIGINT PRIMARY KEY, text VARCHAR(100) CHARACTER SET utf8mb4); ~~ 字符数、字节数和索引长度上限不同；排序相等规则由collation决定。
datetime ~~ DATETIME与TIMESTAMP ~~ DATETIME保存日历时间字段；TIMESTAMP按会话时区转换并有不同范围。按业务语义选择。 ~~ SELECT NOW(), UTC_TIMESTAMP(), @@session.time_zone; ~~ 跨系统记录时间点应统一时区约定；DATETIME不会自动保存来源时区。
primary-key ~~ 主键与唯一约束 ~~ 主键唯一且非NULL；InnoDB以主键组织聚簇索引，二级索引记录主键值。 ~~ CREATE TABLE items (id BIGINT PRIMARY KEY, code VARCHAR(40) UNIQUE); ~~ 可空唯一索引通常允许多个NULL；超宽主键会增加二级索引占用。
foreign-key ~~ 外键约束 ~~ 外键保持父子表引用完整性，可配置删除和更新行为。需要兼容字段类型与索引。 ~~ CREATE TABLE child (id BIGINT PRIMARY KEY, user_id BIGINT,\n FOREIGN KEY (user_id) REFERENCES users(id)); ~~ 级联删除可能扩大影响范围；关闭外键检查导入后不会自动补做全部验证。
check ~~ CHECK约束 ~~ CHECK用于行内条件校验，MySQL8.0.16后开始实施CHECK约束。 ~~ CREATE TABLE grades (id BIGINT PRIMARY KEY, score INT CHECK (score BETWEEN 0 AND 100)); ~~ 条件为UNKNOWN可通过CHECK；需要禁止NULL时同时声明NOT NULL。
alter-add ~~ 新增字段 ~~ ALTER TABLE增加字段，算法和锁行为取决于版本、表结构和具体操作。 ~~ ALTER TABLE users ADD COLUMN email VARCHAR(120); ~~ 不承诺所有DDL无锁；先在接近规模的测试表核验算法与耗时。
alter-modify ~~ 修改字段与索引 ~~ MODIFY更改列完整定义，RENAME COLUMN改名，ADD INDEX增加索引。 ~~ ALTER TABLE users RENAME COLUMN name TO display_name; ~~ 修改会影响应用SQL。MODIFY遗漏原属性可能改变定义；执行前查看SHOW CREATE TABLE。
drop-truncate ~~ 删除对象与清空表 ~~ DROP TABLE删除表结构及数据；TRUNCATE清空表，属于DDL，通常隐式提交。 ~~ DROP TABLE IF EXISTS temporary_example; ~~ 该命令有破坏性；TRUNCATE不能像普通DELETE一样回滚，不要在生产表试验。
''')
    add('数据操作与查询',r'''
insert ~~ 插入数据 ~~ INSERT显式列名避免依赖列顺序，多行VALUES可减少往返。 ~~ INSERT INTO users (id,name,dept,score,created_at)\nVALUES (1,'Ada','dev',90,NOW()),(2,'Lin','ops',80,NOW()); ~~ 重复主键会失败；外部输入使用参数化SQL，不能拼接字符串。
upsert ~~ 唯一冲突更新 ~~ ON DUPLICATE KEY UPDATE在主键或唯一索引冲突时更新已有行。8.4可用行别名引用待插入值。 ~~ INSERT INTO users (id,name) VALUES (1,'Ada') AS incoming\nON DUPLICATE KEY UPDATE name=incoming.name; ~~ 多个唯一索引时冲突目标可能不直观；不要把它当任意业务条件的MERGE。
update ~~ 按条件更新 ~~ UPDATE结合WHERE限定目标，先SELECT核对范围，重要操作在事务内验证结果。 ~~ UPDATE users SET score=score+1 WHERE id=1; ~~ 缺少WHERE会更新所有行。并发读改写可用原子更新或条件版本号。
delete ~~ 按条件删除 ~~ DELETE移除符合条件的行，InnoDB事务中可回滚，提交后需依赖备份或日志恢复。 ~~ START TRANSACTION;\nDELETE FROM users WHERE id=2;\nROLLBACK; ~~ 大批删除需分批和评估锁、undo及复制延迟；外键可能联动其他表。
select ~~ 选择字段 ~~ SELECT明确需要的列，并用别名表达结果意义，减少不必要的传输。 ~~ SELECT id,name,score AS points FROM users; ~~ SELECT *会随表结构变化改变结果，也可能增加IO和网络开销。
where ~~ 条件筛选 ~~ WHERE在分组之前过滤，AND优先级高于OR，复杂条件用括号明确意图。 ~~ SELECT id,name FROM users WHERE dept='dev' AND score>=80; ~~ NULL比较用IS NULL；字段隐式转换可能导致索引或结果问题。
order-by ~~ 排序结果 ~~ ORDER BY指定确定顺序，多列排序可解决相同主排序值时的不确定性。 ~~ SELECT id,score FROM users ORDER BY score DESC,id ASC; ~~ 不带ORDER BY的行顺序没有保证，即使某次恰好按主键排列。
limit ~~ 分页与游标 ~~ LIMIT控制返回行数，深OFFSET可能扫描并丢弃大量记录；大列表可用最后主键续读。 ~~ SELECT id,name FROM users WHERE id>100 ORDER BY id LIMIT 20; ~~ 游标条件必须与排序匹配；排序键可变时需考虑漏读或重复。
distinct ~~ 去重结果 ~~ DISTINCT对全部选出列的组合去重；单列去重与多列组合去重不同。 ~~ SELECT DISTINCT dept FROM users; ~~ DISTINCT不是随意消除错误JOIN重复行的通用补丁。
null ~~ NULL处理 ~~ NULL表示未知或缺失，普通比较通常得到UNKNOWN，在WHERE中不会当成true。 ~~ SELECT id FROM users WHERE dept IS NULL;\nSELECT NULL <=> NULL; ~~ <=>为NULL安全相等；COUNT列忽略NULL而COUNT(*)统计行。
like ~~ 文本匹配 ~~ LIKE中百分号匹配任意长度，下划线匹配一个字符；排序规则影响大小写等匹配。 ~~ SELECT id,name FROM users WHERE name LIKE 'A%'; ~~ 前导通配符通常无法利用普通B树前缀定位；用户输入需处理通配符含义。
in-between ~~ 集合与区间 ~~ IN判断属于给定集合，BETWEEN包含两端。时间查询常用左闭右开区间。 ~~ SELECT id FROM users WHERE score BETWEEN 80 AND 90;\nSELECT id FROM users WHERE created_at>='2024-01-01' AND created_at<'2024-02-01'; ~~ NOT IN子查询出现NULL可能使结果意外为空，考虑NOT EXISTS。
join ~~ 内连接 ~~ INNER JOIN返回满足连接条件的行；多对多关系会乘法增加结果行。 ~~ SELECT a.id,a.name,b.name AS colleague FROM users a\nJOIN users b ON a.dept=b.dept AND a.id<b.id; ~~ 先明确每个连接键的唯一性，避免漏条件导致笛卡尔积。
left-join ~~ 左连接保留主表 ~~ LEFT JOIN保留左表行，未匹配的右表列为NULL。 ~~ SELECT a.id,b.id AS partner FROM users a\nLEFT JOIN users b ON a.dept=b.dept AND b.id<>a.id; ~~ 右表过滤条件放在WHERE可能消除NULL行，效果退化为内连接。
group-by ~~ 分组统计 ~~ GROUP BY按键聚合，同一分组中非聚合字段须符合ONLY_FULL_GROUP_BY等规则。 ~~ SELECT dept,COUNT(*) AS n,AVG(score) AS avg_score\nFROM users GROUP BY dept; ~~ 关闭检查后任取非分组字段会产生不确定结果，优先写清关系。
having ~~ 分组后筛选 ~~ WHERE筛选原始行，HAVING筛选分组聚合后的结果。 ~~ SELECT dept,COUNT(*) AS n FROM users\nWHERE score>=60 GROUP BY dept HAVING COUNT(*)>=2; ~~ 能下推的行条件放WHERE，减少参与分组的数据。
subquery ~~ 标量与集合子查询 ~~ 标量子查询要求最多返回一行一列，多行会报错；集合子查询可与IN配合。 ~~ SELECT id,name FROM users WHERE score>(SELECT AVG(score) FROM users); ~~ 相关子查询的执行方式取决于优化器，不能一律假定逐行完整执行。
exists ~~ EXISTS存在判断 ~~ EXISTS关心子查询是否返回行，适合有无关联记录及反连接判断。 ~~ SELECT a.id FROM users a WHERE EXISTS\n(SELECT 1 FROM users b WHERE b.dept=a.dept AND b.id<>a.id); ~~ 用NOT EXISTS表达不存在时需把完整关联条件写入子查询。
union ~~ UNION合并查询 ~~ UNION合并并去重，UNION ALL保留所有行，通常避免额外去重成本。 ~~ SELECT name FROM users WHERE dept='dev'\nUNION ALL SELECT name FROM users WHERE dept='ops'; ~~ 各分支列数须相同且类型可合并；最终排序应针对整体结果。
cte ~~ WITH公共表表达式 ~~ CTE为单条语句提供可命名结果，提高复杂查询可读性，可递归表达层级遍历。 ~~ WITH high AS (SELECT * FROM users WHERE score>=80)\nSELECT dept,COUNT(*) FROM high GROUP BY dept; ~~ CTE不保证物化或更快，是否合并由优化器和查询结构决定。
recursive ~~ 递归CTE ~~ 递归CTE包含锚点和递归项，并需要明确终止条件与合理深度。 ~~ WITH RECURSIVE seq(n) AS (\n SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n<5\n) SELECT n FROM seq; ~~ 错误终止条件可能触发递归深度限制；层级有环时还需防环。
window ~~ 窗口排序 ~~ 窗口函数在保留明细行的同时按分区计算，ROW_NUMBER给每行唯一序号。 ~~ SELECT id,dept,ROW_NUMBER() OVER\n(PARTITION BY dept ORDER BY score DESC,id) AS rn FROM users; ~~ 窗口结果不能直接在同层WHERE筛选，可套CTE或子查询。
duplicates ~~ 查询重复数据 ~~ 按业务键分组并筛选COUNT大于1，再关联获取具体重复行。 ~~ SELECT name,COUNT(*) AS n FROM users\nGROUP BY name HAVING COUNT(*)>1; ~~ 先明确大小写、空格和NULL是否算相同，collation影响比较。
monthly ~~ 按月份汇总 ~~ 先用原时间列范围过滤，再按月份表达式分组，兼顾范围索引和展示。 ~~ SELECT DATE_FORMAT(created_at,'%Y-%m') AS month,COUNT(*)\nFROM users WHERE created_at>='2024-01-01' AND created_at<'2025-01-01'\nGROUP BY month ORDER BY month; ~~ 不建议为了筛选月份在WHERE中直接给索引列包函数。
''')
    add('常用函数',r'''
count ~~ COUNT与聚合 ~~ COUNT(*)统计行，COUNT(col)统计非NULL值，SUM和AVG通常忽略NULL。 ~~ SELECT COUNT(*),COUNT(score),SUM(score),AVG(score) FROM users; ~~ 没有输入行时COUNT为0，SUM通常为NULL，可用COALESCE处理。
coalesce ~~ COALESCE默认值 ~~ COALESCE从左到右返回首个非NULL表达式，可给缺失值设置展示默认值。 ~~ SELECT id,COALESCE(dept,'unassigned') FROM users; ~~ 默认值替换可能掩盖数据缺失，展示层和统计含义需分开。
case ~~ CASE条件表达式 ~~ CASE按条件返回值，可用于分类和条件聚合。 ~~ SELECT id,CASE WHEN score>=60 THEN 'pass' ELSE 'retry' END AS result FROM users; ~~ 各分支结果类型会合并，避免混用数值与文本造成隐式转换。
concat ~~ 拼接与字符串长度 ~~ CONCAT连接字符串；CHAR_LENGTH计算字符数，LENGTH计算字节数。 ~~ SELECT CONCAT(name,':',COALESCE(dept,'')),CHAR_LENGTH(name),LENGTH(name) FROM users; ~~ CONCAT任一参数为NULL通常返回NULL，使用COALESCE或合适的CONCAT_WS。
substring ~~ 截取与清理字符串 ~~ SUBSTRING位置从1开始，TRIM去除两端指定字符或默认空格。 ~~ SELECT SUBSTRING(name,1,3),TRIM(name),LOWER(name) FROM users; ~~ TRIM默认不是删除所有Unicode空白；大小写转换与字符集有关。
round ~~ 舍入与截断 ~~ ROUND按规则舍入，TRUNCATE按位数截断。精确值和近似值的舍入表现可能不同。 ~~ SELECT ROUND(CAST(1.235 AS DECIMAL(6,3)),2),TRUNCATE(1.239,2); ~~ 货币列先采用精确类型，再明确业务舍入规则。
date-add ~~ 日期加减 ~~ DATE_ADD/DATE_SUB按日历单位移动时间，TIMESTAMPDIFF计算指定单位差值。 ~~ SELECT DATE_ADD('2024-01-31',INTERVAL 1 MONTH);\nSELECT TIMESTAMPDIFF(DAY,'2024-01-01','2024-01-03'); ~~ 月份长短不同会调整结果；时区差异可能影响时间点解释。
json ~~ JSON字段查询 ~~ JSON类型保存有效JSON，可用路径提取值；箭头双大于等价于提取并去引号。 ~~ SELECT JSON_UNQUOTE(JSON_EXTRACT('{"name":"Ada"}','$.name')); ~~ SQL NULL、JSON null和字符串"null"不同；热点字段考虑生成列索引。
rank ~~ RANK与DENSE_RANK ~~ RANK并列后留下序号间隙，DENSE_RANK无间隙，ROW_NUMBER不并列。 ~~ SELECT id,score,RANK() OVER(ORDER BY score DESC) AS r,\nDENSE_RANK() OVER(ORDER BY score DESC) AS dr FROM users; ~~ 想保留同分并列就不要把唯一id加入窗口排序条件。
lag ~~ LAG与累计窗口 ~~ LAG访问前一行；累计SUM需明确窗口范围，避免默认RANGE把同序值合并。 ~~ SELECT id,score,LAG(score) OVER(ORDER BY id) AS previous,\nSUM(score) OVER(ORDER BY id ROWS UNBOUNDED PRECEDING) AS total FROM users; ~~ 没有稳定ORDER BY就没有确定的前一行和累计顺序。
''')
    add('索引与优化',r'''
btree ~~ B树索引基础 ~~ InnoDB常用B树索引支持等值、范围和顺序访问；查询效果取决于选择性和访问行数。 ~~ CREATE INDEX ix_users_score ON users(score); ~~ 索引增加写入和存储成本；低选择性条件不一定走索引。
composite-index ~~ 联合索引顺序 ~~ 联合索引按列顺序组织，左侧前缀与等值/范围条件影响可用范围。 ~~ CREATE INDEX ix_users_dept_score ON users(dept,score); ~~ 不是所有包含索引列的条件都能用全索引定位；以EXPLAIN为准。
covering ~~ 覆盖索引 ~~ 查询所需列都能从选定索引获得时可减少回表，InnoDB二级索引包含主键。 ~~ EXPLAIN SELECT id,score FROM users WHERE score>=80; ~~ 覆盖不代表扫描行数少；为了覆盖添加太多宽字段会扩大索引。
explain ~~ 查看执行计划 ~~ EXPLAIN展示优化器选择的访问方式、索引和估算行数。结合过滤条件及数据分布解读。 ~~ EXPLAIN FORMAT=TREE SELECT id FROM users WHERE score>=80; ~~ 估算不是实测；不同输出格式字段含义不同，避免单看type判断好坏。
explain-analyze ~~ 实测执行计划 ~~ EXPLAIN ANALYZE执行查询并输出实际行数、循环次数与耗时，帮助定位估算偏差。 ~~ EXPLAIN ANALYZE SELECT id FROM users WHERE score>=80; ~~ 这会真的执行查询，复杂查询可能消耗资源；只在已评估场景使用。
sargable ~~ 保持可索引条件 ~~ 避免给索引列无必要地包函数，保持类型一致，时间范围尽量直接比较原列。 ~~ SELECT id FROM users WHERE created_at>='2024-01-01' AND created_at<'2024-01-02'; ~~ 优化器支持部分表达式索引，但不能假定自动改写所有表达式。
slow-log ~~ 慢查询入口 ~~ 慢日志捕获达到阈值的语句，用执行频次、总耗时、扫描行数共同排序排查。 ~~ SHOW VARIABLES LIKE 'slow_query_log';\nSHOW VARIABLES LIKE 'long_query_time'; ~~ 开启和修改参数需权限并评估磁盘；慢日志可能包含业务字面量。
statistics ~~ 统计信息与ANALYZE ~~ 优化器依赖统计信息估计选择性，数据变化大时可考虑更新统计。 ~~ ANALYZE TABLE users;\nSHOW INDEX FROM users; ~~ ANALYZE会消耗资源并影响计划，不把它当所有慢查询的自动修复。
''')
    add('事务与锁',r'''
transaction ~~ 事务提交与回滚 ~~ START TRANSACTION开始事务，COMMIT提交，ROLLBACK放弃未提交修改。 ~~ START TRANSACTION;\nUPDATE users SET score=score+1 WHERE id=1;\nCOMMIT; ~~ DDL可能隐式提交；事务不要跨越长时间用户等待。
isolation ~~ 隔离级别 ~~ InnoDB支持四种标准隔离级别，默认通常REPEATABLE READ。快照读和锁定读行为不同。 ~~ SELECT @@transaction_isolation;\nSET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED; ~~ 不能只凭隔离级别名称推断所有并发现象，还要区分具体语句和锁。
mvcc ~~ MVCC一致性读 ~~ 普通一致性SELECT借助读视图和undo读取合适版本，减少读写阻塞。 ~~ START TRANSACTION WITH CONSISTENT SNAPSHOT;\nSELECT * FROM users;\nCOMMIT; ~~ RR与RC创建读视图时机不同；锁定读不等同于快照读。
for-update ~~ 锁定读取 ~~ SELECT FOR UPDATE锁定待修改记录，配合事务使用并按固定顺序获取锁。 ~~ START TRANSACTION;\nSELECT score FROM users WHERE id=1 FOR UPDATE;\nUPDATE users SET score=score+1 WHERE id=1;\nCOMMIT; ~~ 锁作用于索引访问路径，范围条件可能锁更多记录；事务结束才释放。
gap-lock ~~ 间隙锁与临键锁 ~~ InnoDB在部分隔离和范围访问下使用间隙及next-key锁，避免范围内插入造成某些并发变化。 ~~ SHOW ENGINE INNODB STATUS; ~~ 不能笼统说行锁永远只锁已有一行；缺索引时扫描范围会扩大。
deadlock ~~ 处理死锁与重试 ~~ 并发事务锁顺序冲突可能死锁。被选为牺牲者的事务需整体重试并限制次数。 ~~ SHOW ENGINE INNODB STATUS; ~~ 重试必须符合幂等和事务边界，不能只重试失败的最后一条语句。
''')
    add('管理操作',r'''
show-schema ~~ 查看结构与版本 ~~ 用SHOW CREATE TABLE确认真实列定义、索引、字符集及表选项，用VERSION确认服务版本。 ~~ SELECT VERSION();\nSHOW CREATE TABLE users;\nSHOW TABLE STATUS LIKE 'users'; ~~ 文档基线是8.4，实际目标数据库应独立核验版本和引擎。
privileges ~~ 用户与最小权限 ~~ 用户账号由用户名和来源主机构成。业务账号只授予需要的库表操作。 ~~ SHOW GRANTS FOR CURRENT_USER; ~~ 授权修改需要管理员权限；不要让应用常驻使用root或开放任意来源。
backup ~~ 逻辑导出与恢复 ~~ mysqldump在操作系统终端运行；InnoDB可用single-transaction获得一致性快照，期间避免相关DDL。 ~~ -- Shell, not SQL:\n-- mysqldump --single-transaction -u user -p demo > demo.sql\n-- mysql -u user -p demo < demo.sql ~~ 恢复可能覆盖现有对象，先检查文件并在隔离库演练；密码用交互输入。
status ~~ 运行状态与会话 ~~ PROCESSLIST观察当前会话，performance_schema提供更系统的等待与语句统计。 ~~ SHOW PROCESSLIST;\nSHOW GLOBAL STATUS LIKE 'Threads_connected'; ~~ 能看到多少会话受权限影响；贸然KILL可能回滚大事务并影响业务。
autocommit ~~ 自动提交与连接池 ~~ autocommit控制每条独立语句是否自动提交；从连接池取出会话时确认事务和隔离状态。 ~~ SELECT @@autocommit;\nSET autocommit=1; ~~ 设置autocommit可能提交已有事务，必须在清楚会话状态时使用。
savepoint ~~ 事务保存点 ~~ SAVEPOINT为事务内局部回滚设置标记，回滚保存点不会结束整个事务。 ~~ START TRANSACTION;\nSAVEPOINT before_change;\nUPDATE users SET score=0 WHERE id=1;\nROLLBACK TO SAVEPOINT before_change;\nCOMMIT; ~~ 不是嵌套事务；DDL隐式提交及某些锁行为不能用保存点简单恢复。
prepared ~~ 参数化查询 ~~ 预处理语句把数据值与SQL结构分开，应用通常通过驱动占位符绑定参数。 ~~ PREPARE stmt FROM 'SELECT name FROM users WHERE id=?';\nSET @uid=1;\nEXECUTE stmt USING @uid;\nDEALLOCATE PREPARE stmt; ~~ 参数不能替代任意表名、列名或排序方向，这些需要白名单。
import ~~ 导入文本数据 ~~ LOAD DATA可批量加载文本，需明确字符集、分隔符和目标列。先在暂存表检查数据质量。 ~~ SHOW VARIABLES LIKE 'local_infile';\nSHOW VARIABLES LIKE 'secure_file_priv'; ~~ LOCAL会从客户端读文件，服务端读路径由secure_file_priv等限制；不要为便利无条件开放。
session-timezone ~~ 会话时区 ~~ 每个连接有自己的time_zone，连接池应统一初始化；命名时区依赖服务端时区表。 ~~ SET SESSION time_zone='+00:00';\nSELECT NOW(),UTC_TIMESTAMP(),@@session.time_zone; ~~ 修改会话时区不会把已有DATETIME列自动转为另一个时区。
optimistic ~~ 乐观并发更新 ~~ 用版本或原值作WHERE条件，仅在读到的状态仍有效时更新，受影响行数决定是否冲突。 ~~ UPDATE users SET score=91 WHERE id=1 AND score=90;\nSELECT ROW_COUNT(); ~~ 示例以原值作条件；正式版本字段更清晰，冲突后需重读，不能无限盲重试。
''')
    # Every query card includes the shared schema link; DDL-specific cards explain their own schema.
    for c in out:
        if c['group'] in ('数据操作与查询','常用函数','索引与优化','事务与锁'):
            c['related']=['mysql.create-table']
    return out
