#!/usr/bin/env bash
set -euo pipefail

name="${1:-}"
if [ -z "$name" ]; then
  echo "Usage: ./scripts/new_component.sh component_name"
  exit 1
fi

dir="components/$name"
mkdir -p "$dir/include"

cat > "$dir/CMakeLists.txt" <<EOF
idf_component_register(
    SRCS "$name.c"
    INCLUDE_DIRS "include"
)
EOF

cat > "$dir/include/$name.h" <<EOF
#pragma once

#include "esp_err.h"

esp_err_t ${name}_init(void);
EOF

cat > "$dir/$name.c" <<EOF
#include "$name.h"

esp_err_t ${name}_init(void)
{
    return ESP_OK;
}
EOF

echo "Created component: $dir"
