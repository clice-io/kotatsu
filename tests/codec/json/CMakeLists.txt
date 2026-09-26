kota_add_module_tests(LIBS kota::codec)

# Lets suites of other modules opt into cases that need the JSON codec, such as
# zest's EXPECT_SNAPSHOT_JSON tests.
target_compile_definitions(unit_tests PRIVATE KOTA_TEST_HAS_JSON=1)
target_compile_definitions(system_tests PRIVATE KOTA_TEST_HAS_JSON=1)
