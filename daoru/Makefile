.PHONY: test
test:
	./vendor/bin/phpunit --bootstrap test/bootstrap.php test

.PHONY: import-test-db
import-test-db:
	@	db=$(shell grep -Po '(?<=DATABASE_NAME\=).+$$' test/.env) && \
		user=$(shell grep -Po '(?<=DATABASE_USER\=).+$$' test/.env)	&& \
		pass=$(shell grep -Po '(?<=DATABASE_PASS\=).+$$' test/.env) && \
		port=$(shell grep -Po '(?<=DATABASE_PORT\=).+$$' test/.env) && \
		host=$(shell grep -Po '(?<=DATABASE_HOST\=).+$$' test/.env) && \
		echo "drop tables..." && \
		mysql -D"$$db" -u"$$user" -p"$$pass" -h"$$host" -Nse 'show tables' |\
		 while read table; do \
		 	mysql -D"$$db" -u"$$user" -p"$$pass" -h"$$host" -e 'drop table '$$table -D "$$db"; \
	 	 done && \
	 	echo "setup new tables..." && \
	 	/bin/bash ../install/generate_initialize_sql.sh | mysql -u"$$user" -p"$$user" -D"$$db" -h"$$host"
