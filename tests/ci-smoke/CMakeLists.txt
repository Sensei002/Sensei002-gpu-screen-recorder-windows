# Phase 1 CI toolchain smoke test.
#
# This is NOT the real application build. It validates that the GitHub
# Actions Windows runner can: configure with CMake, invoke the MSVC x64
# toolchain, build, and run the resulting binary. The real production build
# (engine + UI + notify) replaces this target in Phase 2
# (docs/implementation-roadmap.md).
cmake_minimum_required(VERSION 3.25)
project(gsr_ci_smoke C)

add_executable(ci-smoke main.c)
