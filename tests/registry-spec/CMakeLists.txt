# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The OA Registry Specification examples, read with the engine's own readers,
# and the schema check over those examples.
if(BUILD_TESTING)
  add_executable(oa-registry-spec-test registry_spec_test.cpp)
  target_link_libraries(oa-registry-spec-test PRIVATE oa-data-registry oa-data-catalogue
    oa-app-content oa-formats-json oa-base-signing oa-base-sha256 oa-test-support)
  add_test(NAME registry-spec-examples COMMAND oa-registry-spec-test
    ${PROJECT_SOURCE_DIR}/docs/registries/examples)
  set_tests_properties(registry-spec-examples PROPERTIES TIMEOUT 30 LABELS "docs;registry")

  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME registry-spec-schemas COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tools/check_registry_spec.py --root ${PROJECT_SOURCE_DIR})
  add_test(NAME registry-spec-schemas-selftest COMMAND ${Python3_EXECUTABLE}
    ${PROJECT_SOURCE_DIR}/tools/check_registry_spec.py --self-test)
  set_tests_properties(registry-spec-schemas registry-spec-schemas-selftest PROPERTIES TIMEOUT 60
    LABELS "docs;registry")
endif()
