/**
 * test_main.cpp — Test runner entry point
 *
 * Defines TEST_MAIN so that test_framework.hpp emits:
 *   - the global g_current_test_failed variable
 *   - the main() function
 *
 * All test cases are registered from sibling test_*.cpp files via
 * static-initialisation TestRegistrar objects; the linker pulls them in.
 */

#define TEST_MAIN
#include "test_framework.hpp"
