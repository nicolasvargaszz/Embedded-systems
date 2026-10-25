#!/usr/bin/env python3
"""Run portable counter/geometry checks and dataset tests (no boards required)."""
import shutil
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
compiler=shutil.which("g++") or shutil.which("clang++")
if not compiler: raise SystemExit("Install g++ or clang++; on macOS use xcode-select --install")
build=ROOT/"build";build.mkdir(exist_ok=True)
binary=build/("test_core.exe" if sys.platform=="win32" else "test_core")
subprocess.run([compiler,"-std=c++11","-Wall","-Wextra","-Werror","-pedantic",str(ROOT/"tests/test_core.cpp"),"-o",str(binary)],check=True)
subprocess.run([str(binary)],check=True)
subprocess.run([sys.executable,"-m","unittest","discover","-s",str(ROOT/"tests"),"-p","test_*.py","-v"],check=True)
