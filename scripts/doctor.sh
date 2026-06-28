#!/usr/bin/env bash
set -u

score=0
total=0
fail=0

check() {
  total=$((total+1))
  name="$1"
  cmd="$2"
  if eval "$cmd" >/dev/null 2>&1; then
    printf "PASS  %-28s\n" "$name"
    score=$((score+1))
  else
    printf "FAIL  %-28s\n" "$name"
    fail=$((fail+1))
  fi
}

echo "Smart Villa OS Doctor"
echo "=============================="

check "Repo root" 'test -f CMakeLists.txt && test -d components && test -d main'
check "Git repo" 'git rev-parse --is-inside-work-tree'
check "Git remote" 'git remote -v | grep -q origin'
check "AI_START_HERE.md" 'test -f AI_START_HERE.md'
check "AGENTS.md" 'test -f AGENTS.md'
check "CLAUDE.md" 'test -f CLAUDE.md'
check "Project Bible" 'test -f docs/00_PROJECT_BIBLE.md'
check "Security Standard" 'test -f docs/10_SECURITY_STANDARD.md'
check "Coding Standard" 'test -f docs/19_CODING_STANDARD.md'
check ".ai folder" 'test -d .ai'
check ".github folder" 'test -d .github'
check "Engineering folder" 'test -d engineering'
check "ESP-IDF idf.py" 'command -v idf.py'
check "Git" 'command -v git'
check "Python3" 'command -v python3'
check "CMake" 'command -v cmake'
check "Ninja" 'command -v ninja'
check "VS Code CLI" 'command -v code'

echo "=============================="
echo "Score: $score / $total"

if [ "$fail" -eq 0 ]; then
  echo "RESULT: READY ✅"
  exit 0
else
  echo "RESULT: NEEDS ATTENTION ❌"
  exit 1
fi
