#include <nanomodbus.h>
#include <zephyr/ztest.h>

ZTEST(nanomodbus_baseline, test_module_symbols_available) {
    zassert_true(IS_ENABLED(CONFIG_NANOMODBUS), "CONFIG_NANOMODBUS must be enabled");
    nmbs_error err = NMBS_ERROR_NONE;
    zassert_equal(err, 0, "NMBS_ERROR_NONE must evaluate to 0");
}

ZTEST_SUITE(nanomodbus_baseline, NULL, NULL, NULL, NULL, NULL);
