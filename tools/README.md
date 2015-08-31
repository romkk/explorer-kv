# 数据导入工具

该目录下需存放一个`.env`文件，配置了数据库连接方式。

## load_mysql.sh

加载 raw 数据到数据库中。如果目录层级有变，注意调整这里的`k`值：`sort -n -k 4 -t '/'`.

```shell
find ../daoru/importer -type d | tail -n+2 | xargs -n1  | sort -n -k 4 -t '/' | xargs -n 1 ./load_mysql.sh 
```

## load_postprocess_data.sh

加载后处理数据到数据库中，`-P 3`参数指定了最大 3 个并发。

```shell
find ../jiexi.bootstrap/data -name '*.csv' | sort | xargs -n 1 -P 3 ./load_postprocess_data.sh
```