CC ?= gcc
CFLAGS ?= -O3 -Wall -Wextra
BISON ?= bison

BIN_DIR = bin
SRC_DIR = src
TARGET = $(BIN_DIR)/sql_filter
YACC_SRC = $(SRC_DIR)/sql_filter.y
GEN_C = $(SRC_DIR)/sql_filter.c
TEST_DB = /tmp/sql_sanitizer_test.db
PREFIX ?= /usr/local

.PHONY: all clean test benchmark install uninstall

all: $(TARGET)

$(GEN_C): $(YACC_SRC)
	$(BISON) -d $(YACC_SRC) -o $(GEN_C)

$(TARGET): $(GEN_C)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(GEN_C) -o $(TARGET)
	@echo "[+] Build complete: $(TARGET)"

test: $(TARGET)
	@echo "[*] Running end-to-end integration tests..."
	@rm -f $(TEST_DB)
	@echo "[1/2] Testing DSL format input..."
	@printf '%s\n' \
		'Document(id: "doc_001", lang: "en", category: "test") {' \
		"    Here is an injection attack string: '); DROP TABLE raw_corpus; --" \
		"    And another single quote: 'hello world'" \
		'}' \
		'Document(id: "doc_002", lang: "ja", category: "multibyte") {' \
		"    日本語のテキスト検証：'引用符' と 改行を含みます。" \
		'}' | ./$(TARGET) | sqlite3 $(TEST_DB)
	@echo "[2/2] Testing raw plain text stream..."
	@printf '%s\n' \
		"こんにちは'世界" \
		"Another attack: ' OR '1'='1" | ./$(TARGET) --category "raw_log" | sqlite3 $(TEST_DB)
	@COUNT=$$(sqlite3 $(TEST_DB) "SELECT count(*) FROM raw_corpus;"); \
	if [ "$$COUNT" -eq 4 ]; then \
		echo "[+] Test passed: Successfully inserted $$COUNT records into SQLite."; \
		rm -f $(TEST_DB); \
	else \
		echo "[-] Test failed: Expected 4 records, got $$COUNT"; \
		rm -f $(TEST_DB); \
		exit 1; \
	fi

benchmark: $(TARGET)
	@echo "[*] Generating benchmark dataset (10,000 documents)..."
	@python3 -c '\
	with open("/tmp/bench_input.txt", "w") as f:\
	    for i in range(10000):\
	        f.write("Lorem ipsum dolor sit amet. Injection: '\'' OR 1=1; --\n")\
	'
	@echo "[*] Benchmarking raw stream parser throughput..."
	@time ./$(TARGET) < /tmp/bench_input.txt > /dev/null
	@rm -f /tmp/bench_input.txt

clean:
	@rm -rf $(BIN_DIR) $(GEN_C) $(SRC_DIR)/sql_filter.h $(TEST_DB)
	@echo "[*] Clean complete."

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/sql_filter

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/sql_filter