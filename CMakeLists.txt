# SVC-100 — top-level ESP-IDF project file
# Requires ESP-IDF 5.x. Target: esp32s3.
cmake_minimum_required(VERSION 3.16)

# Pull components from the standard ./components directory (implicit) and main/.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(svc-100
        VERSION 0.1.0
        LANGUAGES C CXX)
