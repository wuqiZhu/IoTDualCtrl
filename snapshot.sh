#!/bin/bash
# ==========================================================================
# 快照管理脚本 — 备份/回滚项目文件
# 用法:
#   ./snapshot.sh save    "备注"    # 保存快照
#   ./snapshot.sh list              # 列出所有快照
#   ./snapshot.sh restore <编号>    # 恢复指定快照
#   ./snapshot.sh diff   <编号>    # 对比当前和快照的差异
# ==========================================================================

SNAPSHOT_DIR=".snapshots"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"

mkdir -p "$SNAPSHOT_DIR"

case "$1" in
    save)
        TAG="${2:-未命名}"
        TIME="$(date +%Y%m%d_%H%M%S)"
        DIR="$SNAPSHOT_DIR/$TIME"
        mkdir -p "$DIR"
        echo "备注: $TAG" > "$DIR/INFO.txt"
        echo "时间: $(date '+%Y-%m-%d %H:%M:%S')" >> "$DIR/INFO.txt"

        # 要备份的文件和目录（只备份关键源码，排除编译产物和图片）
        cat > "$DIR/filelist.txt" << 'FILEEOF'
lesson5/rpc_server/*.c
lesson5/rpc_server/*.h
lesson5/rpc_server/www/index.html
lesson5/rpc_server/Makefile
lesson5/rpc_client/*.c
lesson5/rpc_client/*.h
lesson5/rpc_client/Makefile
lesson6/*.c
lesson6/*.h
lesson6/*.cpp
lesson6/Makefile
lesson6/Makefile.static
cloud/*.py
cloud/*.bak
grafana/*.json
grafana/*.yml
grafana/*.yaml
config.json
.env.example
.check*
FILEEOF

        while IFS= read -r pattern; do
            for f in $pattern; do
                [ -f "$f" ] || continue
                target_dir="$DIR/$(dirname "$f")"
                mkdir -p "$target_dir"
                cp -a "$f" "$target_dir/"
            done
        done < "$DIR/filelist.txt"

        echo "[OK] 快照已保存: $TIME  ($TAG)"
        echo "     路径: $DIR"
        ;;

    list)
        echo "可用的快照:"
        echo ""
        for d in "$SNAPSHOT_DIR"/*/; do
            [ -d "$d" ] || continue
            name="$(basename "$d")"
            tag="$(head -1 "$d/INFO.txt" 2>/dev/null | sed 's/备注: //')"
            time="$(sed -n '2p' "$d/INFO.txt" 2>/dev/null | sed 's/时间: //')"
            echo "  [$name]  $time  —  $tag"
        done
        ;;

    restore)
        if [ -z "$2" ]; then
            echo "用法: $0 restore <快照编号>"
            echo "示例: $0 restore 20250320_143000"
            exit 1
        fi
        SRC="$SNAPSHOT_DIR/$2"
        if [ ! -d "$SRC" ]; then
            echo "错误: 快照 $2 不存在"
            echo "执行 '$0 list' 查看所有快照"
            exit 1
        fi

        echo "警告: 将用快照 [$2] 覆盖当前文件！"
        echo -n "确认恢复？(y/N): "
        read -r confirm
        [ "$confirm" != "y" ] && [ "$confirm" != "Y" ] && echo "已取消" && exit 0

        # 读取 filelist.txt 恢复
        if [ -f "$SRC/filelist.txt" ]; then
            while IFS= read -r pattern; do
                # 在快照目录中找对应文件
                for f in $pattern; do
                    snapshot_file="$SRC/$f"
                    [ -f "$snapshot_file" ] || continue
                    target_dir="$(dirname "$f")"
                    mkdir -p "$target_dir"
                    cp -a "$snapshot_file" "$f"
                    echo "  恢复: $f"
                done
            done < "$SRC/filelist.txt"
        else
            # 没有 filelist.txt，全量复制
            cp -a "$SRC/"* "$PROJECT_DIR/"
        fi
        echo "[OK] 已恢复到快照: $2"
        ;;

    diff)
        if [ -z "$2" ]; then
            echo "用法: $0 diff <快照编号>"
            exit 1
        fi
        SRC="$SNAPSHOT_DIR/$2"
        if [ ! -d "$SRC" ]; then
            echo "错误: 快照 $2 不存在"
            exit 1
        fi

        echo "与快照 [$2] 的差异:"
        echo ""
        if [ -f "$SRC/filelist.txt" ]; then
            while IFS= read -r pattern; do
                for f in $pattern; do
                    src_file="$SRC/$f"
                    [ -f "$src_file" ] || continue
                    diff_output=$(diff -q "$src_file" "$f" 2>/dev/null)
                    if [ -n "$diff_output" ]; then
                        echo "  ⚠ $f"
                    fi
                done
            done < "$SRC/filelist.txt"
        fi
        ;;

    *)
        echo "用法:"
        echo "  $0 save    \"备注\"    # 保存快照"
        echo "  $0 list              # 列出快照"
        echo "  $0 restore <编号>    # 恢复快照"
        echo "  $0 diff   <编号>    # 对比差异"
        ;;
esac
