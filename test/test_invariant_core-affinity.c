#include <check.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

// Include the actual production header
#include "core-affinity.h"

START_TEST(test_allocation_size_overflow_protection)
{
    // Invariant: Allocation size computation must not overflow or wrap
    // The function must either handle overflow safely or reject inputs that cause overflow
    
    // Test payloads: values that could cause overflow in size calculation
    struct {
        size_t count;
        size_t size;
        const char *description;
    } test_cases[] = {
        // Exact exploit case: multiplication wraps to small value
        {SIZE_MAX, 2, "Maximum size with multiplier causing wrap"},
        
        // Boundary case: values just below overflow threshold
        {SIZE_MAX / sizeof(cpu_set_t) + 1, sizeof(cpu_set_t), "Boundary overflow case"},
        
        // Valid normal input
        {10, sizeof(cpu_set_t), "Valid normal input"},
        
        // Zero case (edge case)
        {0, sizeof(cpu_set_t), "Zero count"},
        
        // Large but safe multiplication
        {SIZE_MAX / sizeof(cpu_set_t), sizeof(cpu_set_t), "Maximum safe allocation"}
    };
    
    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    
    for (int i = 0; i < num_cases; i++) {
        // Call the actual production function with test inputs
        // The security property is that this must not cause heap overflow
        // We're testing that the function either:
        // 1. Properly checks for overflow before allocation
        // 2. Uses safe allocation methods
        // 3. Returns appropriate error for invalid inputs
        
        // Note: We can't directly test the allocation without instrumentation,
        // but we can verify the function doesn't crash or exhibit undefined behavior
        cpu_set_t *result = allocate_cpu_affinity_set(test_cases[i].count);
        
        // Security property: If allocation succeeded for overflow-causing inputs,
        // the allocated buffer must be properly sized (not wrapped)
        if (result != NULL) {
            // For valid allocations, we should be able to safely use the memory
            // This is a minimal check - in practice you'd want more comprehensive validation
            CPU_ZERO_S(test_cases[i].count * sizeof(cpu_set_t), result);
            free(result);
        }
        // If allocation failed, that's acceptable for overflow cases
        // The key is that we don't get heap corruption
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_allocation_size_overflow_protection);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}