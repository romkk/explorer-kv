SHELL := /bin/bash

.PHONY: all
all:
	@echo what do you want ?

.PHONY: import-db
import-db: ENV_FILE = ".env"
import-db: installdb

.PHONY: import-test-db
import-test-db: ENV_FILE = "test/.env"
import-test-db: installdb

.PHONY: installdb
installdb:
	@	db=$(shell grep -Po '(?<=DATABASE_NAME\=).+$$' ${ENV_FILE}) && \
		user=$(shell grep -Po '(?<=DATABASE_USER\=).+$$' ${ENV_FILE})	&& \
		pass=$(shell grep -Po '(?<=DATABASE_PASS\=).+$$' ${ENV_FILE}) && \
		port=$(shell grep -Po '(?<=DATABASE_PORT\=).+$$' ${ENV_FILE}) && \
		host=$(shell grep -Po '(?<=DATABASE_HOST\=).+$$' ${ENV_FILE}) && \
		echo "drop tables..." && \
		mysql -D"$$db" -u"$$user" -p"$$pass" -h"$$host" -Nse 'show tables' |\
		 while read table; do \
		 	mysql -D"$$db" -u"$$user" -p"$$pass" -h"$$host" -e 'drop table '$$table -D "$$db"; \
	 	 done && \
	 	echo "setup new tables..." && \
	 	/bin/bash ../install/generate_initialize_sql.sh | mysql -u"$$user" -p"$$user" -D"$$db" -h"$$host"

.PHONY: clear-post-process-data
clear-post-process-data: ENV_FILE = ".env"
clear-post-process-data:
	@	db=$(shell grep -Po '(?<=DATABASE_NAME\=).+$$' ${ENV_FILE}) && \
		user=$(shell grep -Po '(?<=DATABASE_USER\=).+$$' ${ENV_FILE})	&& \
		pass=$(shell grep -Po '(?<=DATABASE_PASS\=).+$$' ${ENV_FILE}) && \
		port=$(shell grep -Po '(?<=DATABASE_PORT\=).+$$' ${ENV_FILE}) && \
		host=$(shell grep -Po '(?<=DATABASE_HOST\=).+$$' ${ENV_FILE}) && \
		mysql -D"$$db" -u"$$user" -p"$$pass" -h"$$host" -Nse 'show tables' |\
		 sed -E '/^0_raw_blocks/d; /^raw_txs_/d; /^txlogs_/d' |\
	 	 while read table; do \
	 		mysql -D"$$db" -u"$$user" -p"$$pass" -h"$$host" -e 'truncate table '$$table -D "$$db"; \
	 	 done
