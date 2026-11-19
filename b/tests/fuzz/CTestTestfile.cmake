# CMake generated Testfile for 
# Source directory: /private/tmp/pca/tests/fuzz
# Build directory: /private/tmp/pca/b/tests/fuzz
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(pca9685_fuzz_pca9685_corpus "/private/tmp/pca/b/tests/fuzz/fuzz_replay_fuzz_pca9685" "/private/tmp/pca/tests/fuzz/corpus/fuzz_pca9685")
set_tests_properties(pca9685_fuzz_pca9685_corpus PROPERTIES  _BACKTRACE_TRIPLES "/private/tmp/pca/tests/fuzz/CMakeLists.txt;21;add_test;/private/tmp/pca/tests/fuzz/CMakeLists.txt;0;")
