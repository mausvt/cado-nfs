#include "cado.h"
#include <cstdint>     /* AIX wants it first (it's a bug) */
#include <cstdlib>
#include <climits>
#include "modint.hpp"
#include "tests_common.h"

template <typename INTEGER>
class Tests {
    typedef INTEGER Integer;

    bool test_operator_assign_cxx_mpz() {
        Integer i;
        cxx_mpz z;
        
        i = 1;
        i = z;
        if (i != 0) {
            std::cerr << "Setting to 0 failed" << std::endl;
            return false;
        }
        z = 5;
        i = z;
        if (i != 5) {
            std::cerr << "Setting to 5 failed" << std::endl;
            return false;
        }
        return true;
    }
public:
    bool runTests() {
        bool ok = true;
        ok &= test_operator_assign_cxx_mpz();
        return ok;
    }
};

int main(int argc, const char **argv) {
    bool ok = true;
  
    tests_common_cmdline(&argc, &argv, PARSE_SEED | PARSE_ITER);

    Tests<Integer64> test1;
    ok &= test1.runTests();
    
    Tests<Integer128> test2;
    ok &= test2.runTests();
  
    tests_common_clear();
    exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
}
