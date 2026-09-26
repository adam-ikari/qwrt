# qzjs — 命令入口
#
# 常用：
#   make build      配置 + 编译 Release（-DQZ_BUILD_EXAMPLES=ON）→ build/qzjs qzc qzjs-rt
#   make qzjs       运行 qzjs CLI（make qzjs ARGS='-e "console.log(1)"'）
#   make run        同上（alias）
#   make qzc        用 qzc 编译 JS → 字节码（make qzc SRC=app.js OUT=app.bc）
#   make bc         qzc 编译 + qzjs --bytecode 运行（SRC=app.js [ARGS='...']）
#   make example    make example NAME=fs     运行某个 JS example
#   make grpc       QZ_WITH_GRPC=ON 构建（build_grpc）+ 跑 grpc-hello
#   make test       构建测试套件并跑 ctest
#   make test-offline  只跑 offline 标签（fresh clone 缺 test262 时的正确默认）
#   make docs       构建文档站（docs/.vitepress/dist）
#   make docs-dev   VitePress dev server
#   make clean      删除全部构建目录

BUILD_DIR   ?= build
GRPC_DIR    ?= build_grpc
DOCS_DIR    ?= docs
CMAKE       ?= cmake
BUILD_TYPE  ?= Release

.PHONY: all build qzjs run qzc bc example grpc test test-offline docs docs-dev clean

all: build

## 配置 + 编译（Release，含 examples）→ build/qzjs qzc qzjs-rt
build:
	$(CMAKE) -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DQZ_BUILD_EXAMPLES=ON
	$(CMAKE) --build $(BUILD_DIR) --parallel

## 运行 qzjs CLI（make qzjs ARGS='-e "console.log(1)"' 或 make qzjs ARGS='script.js'）
qzjs:
	$(CMAKE) --build $(BUILD_DIR) --parallel
	./$(BUILD_DIR)/qzjs $(ARGS)

## 运行 qzjs CLI（alias）
run: qzjs

## qzc 编译 JS → 字节码（make qzc SRC=app.js [OUT=app.bc]；缺省 <SRC>.bc）
qzc:
	@test -n "$(SRC)" || { echo "usage: make qzc SRC=app.js [OUT=app.bc]"; exit 2; }
	$(CMAKE) --build $(BUILD_DIR) --target qz_qzc --parallel
	./$(BUILD_DIR)/qzc $(SRC) $(if $(OUT),-o $(OUT))

## qzc 编译 + qzjs --bytecode 运行（make bc SRC=app.js [ARGS='a b']）
bc: qzc
	./$(BUILD_DIR)/qzjs --bytecode $(if $(OUT),$(OUT),$(SRC:.js=.bc)) $(ARGS)

## 运行某个 JS example（make example NAME=fs；C 示例是独立可执行文件）
example: build
	./$(BUILD_DIR)/qzjs examples/$(NAME)/$(NAME).js

## QZ_WITH_GRPC=ON 构建（build_grpc）+ 跑 grpc-hello 四形态
grpc:
	$(CMAKE) -B $(GRPC_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DQZ_WITH_GRPC=ON
	$(CMAKE) --build $(GRPC_DIR) --target qz_cli qz_rt --parallel
	./$(GRPC_DIR)/qzjs examples/grpc-hello/grpc-hello.js

## 构建测试套件并跑全部测试（含 test262；fresh clone 请用 test-offline）
test:
	$(CMAKE) -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DQZ_BUILD_TESTS=ON
	$(CMAKE) --build $(BUILD_DIR) --parallel
	cd $(BUILD_DIR) && ctest --output-on-failure

## 只跑 offline 标签测试（fresh clone 的正确默认：test262 corpus 未初始化）
test-offline:
	$(CMAKE) -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DQZ_BUILD_TESTS=ON
	$(CMAKE) --build $(BUILD_DIR) --parallel
	cd $(BUILD_DIR) && ctest -L offline --output-on-failure

## 构建文档站（docs/.vitepress/dist）
docs:
	npm ci --prefix $(DOCS_DIR)
	npm run build --prefix $(DOCS_DIR)

## VitePress dev server（本地预览）
docs-dev:
	npm run dev --prefix $(DOCS_DIR)

## 清理构建产物
clean:
	rm -rf $(BUILD_DIR) $(GRPC_DIR)
