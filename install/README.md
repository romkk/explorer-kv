## 初始化数据库

```shell
bash ./generate_initialize_sql.sh | mysql -uroot -proot
```

## 删除数据库中所有表

```shell
mysql -D BitcoinExplorerDB -uroot -proot -Nse 'show tables' | while read table; do mysql -uroot -proot -e 'drop table '$table -D BitcoinExplorerDB; done
```

## 清空数据库中所有表

```shell
mysql -D BitcoinExplorerDB -uroot -proot -Nse 'show tables' | while read table; do mysql -uroot -proot -e 'truncate table '$table -D BitcoinExplorerDB; done
```