#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
对单个源文件做「只检查语法与类型」的编译（-fsyntax-only）。

用途：改完 C 代码后，在不触发完整构建、不产出任何 .o/.bin 的前提下，
提前抓出语法错误、类型错误、未声明符号。编译参数直接复用
build/compile_commands.json 里 crid_web.c 的那一条，所以 -I 路径、
宏定义、-std 都与真实构建完全一致。

用法：
    python tools/syntax_check.py main/crid_log.c main/crid_ota.c
"""

import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, "build", "compile_commands.json")


def load_template():
    with open(DB, "r", encoding="utf-8") as f:
        db = json.load(f)
    for e in db:
        if e["file"].replace("\\", "/").endswith("main/crid_web.c"):
            return e["command"]
    raise SystemExit("compile_commands.json 里找不到 crid_web.c 的编译命令")


def check(base, src):
    cmd = re.sub(r"-o\s+\S+", "", base, count=1)
    cmd = re.sub(r"-c\s+\S+\.c\b", lambda m: "-fsyntax-only " + src, cmd, count=1)
    # 不用 shell=True：启用蓝牙后头文件路径变多，命令行超过 cmd.exe 的 8191 字符
    # 上限会直接报「命令行太长」。把整条命令行直接交给 CreateProcess（上限
    # 32767）即可，同时保留 -DIDF_VER=\"...\" 这类反斜杠转义的原样写法。
    p = subprocess.run(cmd, shell=False, capture_output=True,
                       text=True, errors="replace")
    out = (p.stdout or "") + (p.stderr or "")
    return p.returncode, out


def main():
    srcs = sys.argv[1:]
    if not srcs:
        srcs = ["main/crid_log.c"]
    if not os.path.isfile(DB):
        raise SystemExit("缺少 build/compile_commands.json，请先成功构建一次")
    base = load_template()

    fail = 0
    for s in srcs:
        path = os.path.abspath(os.path.join(ROOT, s)) if not os.path.isabs(s) else s
        if not os.path.isfile(path):
            print("MISS %s" % s)
            fail += 1
            continue
        rc, out = check(base, '"%s"' % path)
        name = os.path.basename(s)
        if rc == 0:
            print("OK   %s" % name)
        else:
            fail += 1
            print("FAIL %s" % name)
            for line in out.strip().splitlines()[-40:]:
                print("     " + line)

    print("\n%s（%d 个文件失败）" % ("ALL PASS" if not fail else "FAILED", fail))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
