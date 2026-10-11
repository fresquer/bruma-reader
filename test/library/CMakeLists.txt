add_executable(library_catalog_test
  LibraryStoreTest.cpp
  ${REPO_ROOT}/src/LibraryStore.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
)
target_include_directories(library_catalog_test BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fakes
  ${REPO_ROOT}/test/reader_regressions/fakes
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/FsHelpers
)
# Assertions are this standalone scenario suite's checks, including in Release.
target_compile_options(library_catalog_test PRIVATE -UNDEBUG)
add_test(NAME library_catalog COMMAND library_catalog_test)
