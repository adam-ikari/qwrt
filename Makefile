# qzjs — 命令入口
#
# 推荐用法：
#   make build      配置 + 编译 Release（-DQZ_BUILD_EXAMPLES=ON）
#   make qzjs       运行 qzjs CLI（交互：make qzjs ARGS='-e "console.log(1)"'）
#   make run        同上（alias）
#   make example    make example NAME=fs     运行某个 example
#   make test       构建测试套件并跑 ctest
#   make clean      删除 build 目录

BUILD_DIR ?= build
CMAKE    ?= cmake
BUILD_TYPE ?= Release

.PHONY: all build qzjs run example test clean

all: build

## 配置 + 编译（Release，含 examples）
build:
	$(CMAKE) -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DQZ_BUILD_EXAMPLES=ON
	$(CMAKE) --build $(BUILD_DIR) --parallel

## 运行 qzjs CLI（make qzjs ARGS='-e "console.log(1)"' 或 make qzjs ARGS='script.js'）
qzjs:
	$(CMAKE) --build $(BUILD_DIR) --parallel
	./$(BUILD_DIR)/qzjs $(ARGS)

## 运行 qzjs CLI（alias）
run: qzjs

## 运行某个 example（make example NAME=fs；C 示例见 examples/*/）
example: build
	./$(BUILD_DIR)/qzjs examples/$(NAME)/$(NAME).js

## 构建测试套件并跑全部测试
test:
	$(CMAKE) -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DQZ_BUILD_TESTS=ON
	$(CMAKE) --build $(BUILD_DIR) --parallel
	cd $(BUILD_DIR) && ctest --output-on-failure

## 清理构建产物
clean:
	rm -rf $(BUILD_DIR)
