CC ?= gcc
CFLAGS ?= -O3 -Wall -Wextra -std=c99
BISON ?= bison

SRC_DIR = src
BIN_DIR = bin
TARGET = $(BIN_DIR)/sql_filter
TEST_DB = /tmp/sql_sanitizer_test.db
PREFIX ?= /usr/local

.PHONY: all clean test benchmark install uninstall

all: $(TARGET)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(SRC_DIR)/sql_filter.c: $(SRC_DIR)/sql_filter.y
	$(BISON) -d $(SRC_DIR)/sql_filter.y -o $(SRC_DIR)/sql_filter.c

$(TARGET): $(BIN_DIR) $(SRC_DIR)/sql_filter.c
	$(CC) $(CFLAGS) $(SRC_DIR)/sql_filter.c -o $(TARGET)

clean:
	rm -rf $(BIN_DIR)
	rm -f $(SRC_DIR)/sql_filter.c $(SRC_DIR)/sql_filter.h
	rm -f $(TEST_DB)

test: $(TARGET)
	@echo "[*] Running end-to-end integration tests..."
	@rm -f $(TEST_DB)
	@echo "[1/4] Testing DSL format input..."
	@printf '%s\n' \
		'Document(id: "doc_001", lang: "en", category: "test") {' \
		"  First sample text with 'quotes' and ); DROP TABLE users; --" \
		'}' \
		'Document(id: "doc_002", lang: "ja", category: "audit") {' \
		'  日本語テスト文章です。NUL文字や特殊記号の安全化を検証。' \
		'}' | ./$(TARGET) | sqlite3 $(TEST_DB) > /dev/null
	@echo "[2/4] Testing raw plain text stream..."
	@printf '%s\n' \
		"Plain line 1: It's a test" \
		"Plain line 2: Another safe injection line: '); DELETE FROM raw_corpus; --" | \
		./$(TARGET) | sqlite3 $(TEST_DB) > /dev/null
	@echo "[3/4] Testing column customization & column omission (--no-doc_id, --no-lang, --col-raw_text)..."
	@printf '%s\n' \
		"Custom column text with quotes: 'quoted'" | \
		./$(TARGET) -t "custom_corpus" --no-doc_id --no-lang --col-raw_text "body" --col-created_at "inserted_time" | \
		sqlite3 $(TEST_DB) > /dev/null
	@echo "[4/4] Testing CLI option injection sanitization (-i, -c, -l with quotes)..."
	@printf '%s\n' \
		"Option injection test line" | \
		./$(TARGET) -i "user's_id'); --" -c "cat'egory" -l "j'a" | \
		sqlite3 $(TEST_DB) > /dev/null
	@COUNT_RAW=$$(sqlite3 $(TEST_DB) "SELECT count(*) FROM raw_corpus;"); \
	COUNT_CUSTOM=$$(sqlite3 $(TEST_DB) "SELECT count(*) FROM custom_corpus;"); \
	COLS_CUSTOM=$$(sqlite3 $(TEST_DB) "PRAGMA table_info(custom_corpus);" | cut -d'|' -f2 | tr '\n' ','); \
	INJECTED_DOC_ID=$$(sqlite3 $(TEST_DB) "SELECT doc_id FROM raw_corpus WHERE raw_text = 'Option injection test line';"); \
	if [ "$$COUNT_RAW" -eq 5 ] && [ "$$COUNT_CUSTOM" -eq 1 ] && [ "$$COLS_CUSTOM" = "id,category,body,inserted_time," ] && [ "$$INJECTED_DOC_ID" = "user's_id'); --" ]; then \
		echo "[+] Test passed: Successfully verified stream processing, option customization, and rigorous CLI escaping."; \
		rm -f $(TEST_DB); \
	else \
		echo "[-] Test failed: raw_corpus=$$COUNT_RAW (expected 5), custom_corpus=$$COUNT_CUSTOM (expected 1), injected_id=$$INJECTED_DOC_ID"; \
		rm -f $(TEST_DB); \
		exit 1; \
	fi

benchmark: $(TARGET)
	@echo "[*] Generating synthetic high-volume stream (100k records)..."
	@python3 -c '\
for i in range(100000):\
    print(f"Document(id: \"doc_{i}\", lang: \"ja\", category: \"benchmark\") {{ High throughput benchmark line {i} with quotes: ''test'' and symbols }}")\
' | time ./$(TARGET) > /dev/null
	@echo "[+] Benchmark finished."

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/
	if [ -f doc/sql_filter.1 ]; then \
		install -d $(DESTDIR)$(PREFIX)/share/man/man1; \
		install -m 644 doc/sql_filter.1 $(DESTDIR)$(PREFIX)/share/man/man1/; \
	fi

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/sql_filter
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/sql_filter.1