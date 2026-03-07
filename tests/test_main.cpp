#include "test_framework.h"

#include "utils/logger.h"

#include <exception>
#include <iostream>

int main()
{
    sakura::utils::Logger::Init("logs/sakura-tests.log");

    int passed = 0;
    int failed = 0;

    for (const auto& test : sakura::tests::Registry())
    {
        try
        {
            std::cout << "[RUN ] " << test.name << std::endl;
            test.fn();
            ++passed;
            std::cout << "[PASS] " << test.name << std::endl;
        }
        catch (const std::exception& ex)
        {
            ++failed;
            std::cerr << "[FAIL] " << test.name << std::endl << "  " << ex.what() << std::endl;
        }
        catch (...)
        {
            ++failed;
            std::cerr << "[FAIL] " << test.name << std::endl
                      << "  unknown exception" << std::endl;
        }
    }

    std::cout << "Tests passed: " << passed << ", failed: " << failed << std::endl;
    sakura::utils::Logger::Shutdown();
    return failed == 0 ? 0 : 1;
}