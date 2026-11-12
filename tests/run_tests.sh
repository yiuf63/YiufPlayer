#!/usr/bin/env bash
# ============================================================================
# Muvik 测试运行器
#
# 用法：
#   ./tests/run_tests.sh            # 运行全部单元测试
#   ./tests/run_tests.sh -v         # 显示详细输出
#   ./tests/run_tests.sh parser     # 只运行名字匹配的测试
#
# 设计说明：
#   本项目不依赖 gtest/catch2 等第三方测试框架，测试以独立可执行文件形式存在，
#   由本脚本统一编译并运行。每个测试文件遵循 tests/test_framework.hpp 的约定，
#   失败时以非零退出码返回。
# ============================================================================

set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

ROOT="$(pwd)"
BUILD_DIR="$ROOT/.build-tests"
CXX="${CXX:-c++}"
CXXFLAGS="-std=c++17 -I$ROOT/include -I$ROOT/tests -isystem $ROOT/external/borealis/library/include/borealis/extern -O1"
FILTER=""
VERBOSE=0

while [ $# -gt 0 ]; do
    case "$1" in
        -v|--verbose) VERBOSE=1 ;;
        -*) echo "未知选项: $1"; exit 1 ;;
        *)  FILTER="$1" ;;
    esac
    shift
done

mkdir -p "$BUILD_DIR"

# 需要额外源文件与链接参数的测试在此登记
# 格式: 名称|源文件|附加源文件|额外编译参数
# 需要 fmt / borealis 头文件的测试单独处理
BOREALIS_INC="-I$ROOT/external/borealis/library/include -isystem $ROOT/external/borealis/library/include/borealis/extern -I$ROOT/external/borealis/library/include/borealis/extern/nanovg -I$ROOT/external/borealis/library/lib/extern/fmt/include"
QUICKJS_INC="-I$ROOT/external/quickjs"
FMT_LIB=""
for f in "$ROOT"/build/borealis/lib/extern/fmt/libfmt.a "$ROOT"/.build-tests/libfmt.a; do
    [ -f "$f" ] && FMT_LIB="$f" && break
done
QUICKJS_LIB=""
for f in "$ROOT"/build/quickjs/libquickjs.a "$ROOT"/.build-tests/libquickjs.a; do
    [ -f "$f" ] && QUICKJS_LIB="$f" && break
done

PLUGIN_SRCS="src/plugin/plugin_manager.cpp src/plugin/plugin_config_store.cpp src/plugin/js_plugin.cpp src/plugin/js_runtime.cpp src/utils/crypto_utils.cpp src/utils/app_paths.cpp src/utils/media_name_parser.cpp"
PLUGIN_FLAGS="$BOREALIS_INC $QUICKJS_INC $FMT_LIB $QUICKJS_LIB $(pkg-config --libs libcurl) -ldl -lpthread"

TESTS=(
    "media_name_parser|tests/unit/test_media_name_parser.cpp|src/utils/media_name_parser.cpp|"
    "metadata_store|tests/unit/test_metadata_store.cpp|src/model/metadata_store.cpp src/utils/app_paths.cpp|"
    "media_library|tests/unit/test_media_library.cpp|src/model/media_library.cpp src/utils/scanner.cpp src/utils/app_paths.cpp src/utils/i18n.cpp src/utils/resource_locator.cpp|-DTESTING_WITHOUT_BOREALIS"
    "concurrency|tests/unit/test_concurrency.cpp|src/model/media_library.cpp src/model/metadata_store.cpp src/utils/scanner.cpp src/utils/app_paths.cpp src/utils/playback_history.cpp src/utils/i18n.cpp src/utils/resource_locator.cpp|-lpthread -DTESTING_WITHOUT_BOREALIS"
    "plugin_config|tests/unit/test_plugin_config.cpp|src/plugin/plugin_config_store.cpp src/utils/app_paths.cpp|"
    "playback_history|tests/unit/test_playback_history.cpp|src/utils/playback_history.cpp src/utils/app_paths.cpp|"
    "display_mode|tests/unit/test_display_mode.cpp|src/utils/display_mode.cpp src/utils/app_paths.cpp src/utils/i18n.cpp src/utils/resource_locator.cpp|-DTESTING_WITHOUT_BOREALIS"
    "input_settings|tests/unit/test_input_settings.cpp|src/utils/input_settings.cpp src/utils/app_paths.cpp|-DTESTING_WITHOUT_BOREALIS"
    "keymap|tests/unit/test_keymap.cpp|src/utils/keymap.cpp src/utils/input_settings.cpp src/utils/app_paths.cpp src/utils/resource_locator.cpp src/utils/i18n.cpp|-DTESTING_WITHOUT_BOREALIS"
    "i18n|tests/unit/test_i18n.cpp|src/utils/i18n.cpp src/utils/app_paths.cpp src/utils/resource_locator.cpp|$BOREALIS_INC -DTESTING_WITHOUT_BOREALIS"
    "theme|tests/unit/test_theme.cpp|src/theme/muvik_theme.cpp src/ui/design.cpp src/utils/app_paths.cpp src/utils/i18n.cpp src/utils/resource_locator.cpp|$BOREALIS_INC -DTESTING_WITHOUT_BOREALIS"
    "workshop_dlc|tests/unit/test_workshop_dlc.cpp|$PLUGIN_SRCS|$PLUGIN_FLAGS"
    "scraper|tests/unit/test_scraper.cpp|$PLUGIN_SRCS|$PLUGIN_FLAGS"
    "js_plugin|tests/unit/test_js_plugin.cpp|$PLUGIN_SRCS|$PLUGIN_FLAGS"
)

PASS=0
FAIL=0
FAILED_NAMES=()

for entry in "${TESTS[@]}"; do
    IFS='|' read -r name src extra flags <<< "$entry"
    if [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]]; then
        continue
    fi

    out="$BUILD_DIR/$name"
    printf "\033[36m>>> 编译 %s\033[0m\n" "$name"
    if ! $CXX $CXXFLAGS $flags -o "$out" "$src" $extra 2>"$BUILD_DIR/$name.err"; then
        printf "\033[31m    编译失败\033[0m\n"
        sed 's/^/    /' "$BUILD_DIR/$name.err" | head -20
        FAIL=$((FAIL+1)); FAILED_NAMES+=("$name(编译)")
        continue
    fi

    echo "$out" >/dev/null
    # scraper 测试若存在本地 WebDAV 服务则自动启用其浏览用例
    export MUVIK_TEST_DAV=""
    if [ "$name" = "scraper" ] && [ -f "$ROOT/tests/tools/dav_test_server.py" ]; then
        DAVROOT=$(mktemp -d)
        mkdir -p "$DAVROOT/电影" && : > "$DAVROOT/电影/sample.mp4"
        python3 "$ROOT/tests/tools/dav_test_server.py" "$DAVROOT" 18099 u p >/dev/null 2>&1 &
        DAV_PID=$!
        sleep 1
        if curl -s -o /dev/null -X PROPFIND -H "Depth: 1" -u u:p http://127.0.0.1:18099/ 2>/dev/null; then
            export MUVIK_TEST_DAV="http://127.0.0.1:18099/"
        fi
    fi

    if [ "$VERBOSE" -eq 1 ]; then
        "$out"; rc=$?
    else
        "$out" >"$BUILD_DIR/$name.log" 2>&1; rc=$?
    fi
    if [ -n "${DAV_PID:-}" ]; then
        kill "$DAV_PID" 2>/dev/null; wait "$DAV_PID" 2>/dev/null
        rm -rf "$DAVROOT" 2>/dev/null
        DAV_PID=""
    fi

    if [ $rc -eq 0 ]; then
        PASS=$((PASS+1))
        printf "\033[32m    通过\033[0m\n"
    else
        FAIL=$((FAIL+1)); FAILED_NAMES+=("$name")
        printf "\033[31m    失败\033[0m\n"
        [ "$VERBOSE" -eq 0 ] && grep -E "FAIL|RESULT" "$BUILD_DIR/$name.log" | head -10 | sed 's/^/    /'
    fi
done

# 需要 fmt 的安装器测试（独立编译，避免污染主流程）
if [ -z "$FILTER" ] || [[ "plugin_installer" == *"$FILTER"* ]]; then
    printf "\033[36m>>> 编译 plugin_installer\033[0m\n"
    out="$BUILD_DIR/plugin_installer"
    if $CXX $CXXFLAGS $BOREALIS_INC -o "$out" \
        tests/unit/test_plugin_installer.cpp \
        src/plugin/plugin_installer.cpp $PLUGIN_SRCS \
        $PLUGIN_FLAGS \
        2>"$BUILD_DIR/plugin_installer.err"; then
        if [ "$VERBOSE" -eq 1 ]; then
            "$out" | grep -v INFO; rc=${PIPESTATUS[0]}
        else
            "$out" >"$BUILD_DIR/plugin_installer.log" 2>&1; rc=$?
        fi
        if [ $rc -eq 0 ]; then
            PASS=$((PASS+1)); printf "\033[32m    通过\033[0m\n"
        else
            FAIL=$((FAIL+1)); FAILED_NAMES+=("plugin_installer")
            printf "\033[31m    失败\033[0m\n"
            [ "$VERBOSE" -eq 0 ] && grep -E "FAIL|RESULT" "$BUILD_DIR/plugin_installer.log" | head -10 | sed 's/^/    /'
        fi
    else
        FAIL=$((FAIL+1)); FAILED_NAMES+=("plugin_installer(编译)")
        printf "\033[31m    编译失败\033[0m\n"
        sed 's/^/    /' "$BUILD_DIR/plugin_installer.err" | head -15
    fi
fi

echo
echo "============================================================"
printf " 通过: \033[32m%d\033[0m   失败: " "$PASS"
if [ "$FAIL" -eq 0 ]; then
    printf "\033[32m0\033[0m\n"
else
    printf "\033[31m%d\033[0m\n" "$FAIL"
fi
if [ "$FAIL" -ne 0 ]; then
    printf " 失败项: %s\n" "${FAILED_NAMES[*]}"
fi
echo "============================================================"

[ "$FAIL" -eq 0 ] || exit 1
