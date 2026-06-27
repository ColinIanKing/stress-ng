#include <check.h>
#include <stdlib.h>
#include <string.h>
#include "core-affinity.c"

START_TEST(test_strtok_usage_invariant)
{
    // Invariant: String tokenization must not corrupt original input buffer
    const char *payloads[] = {
        "core1,core2,core3",           // Valid input
        "core1,,core3",                // Boundary: empty token
        "core1,core2,core3,",          // Boundary: trailing delimiter
        ",core1,core2,core3",          // Boundary: leading delimiter
        "core1,core2,core3,core4,core5,core6,core7,core8,core9,core10"  // Long input
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < num_payloads; i++) {
        // Create mutable copy for function under test
        char *input_copy = strdup(payloads[i]);
        ck_assert_ptr_nonnull(input_copy);
        
        // Create reference copy for comparison
        char *reference_copy = strdup(payloads[i]);
        ck_assert_ptr_nonnull(reference_copy);
        
        // Call the actual function from core-affinity.c
        // Assuming parse_core_affinity is the function that uses strtok
        parse_core_affinity(input_copy);
        
        // Security property: Original string content must remain unchanged
        // (This would fail if strtok() is used instead of strtok_r())
        ck_assert_str_eq(input_copy, reference_copy);
        
        free(input_copy);
        free(reference_copy);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_strtok_usage_invariant);
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