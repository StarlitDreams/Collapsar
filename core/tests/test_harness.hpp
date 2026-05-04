#pragma once

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

// Tiny self-contained test harness — no external dep so the core test suite
// always builds. Each test file declares COLLAPSAR_TEST(name) blocks; main()
// runs them and exits non-zero on the first failure.

namespace collapsar::test {

struct Failure : public std::exception {
    std::string message;
    explicit Failure(std::string m) : message{std::move(m)} {}
    const char* what() const noexcept override { return message.c_str(); }
};

#define COLLAPSAR_REQUIRE(expr)                                                \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::ostringstream _oss;                                           \
            _oss << "REQUIRE(" #expr ") failed at " << __FILE__ << ':' << __LINE__; \
            throw ::collapsar::test::Failure{_oss.str()};                      \
        }                                                                      \
    } while (0)

#define COLLAPSAR_REQUIRE_EQ(a, b)                                             \
    do {                                                                       \
        const auto _av = (a);                                                  \
        const auto _bv = (b);                                                  \
        if (!(_av == _bv)) {                                                   \
            std::ostringstream _oss;                                           \
            _oss << "REQUIRE_EQ(" #a ", " #b ") failed at " << __FILE__ << ':' << __LINE__ \
                 << " (lhs=" << _av << " rhs=" << _bv << ')';                  \
            throw ::collapsar::test::Failure{_oss.str()};                      \
        }                                                                      \
    } while (0)

struct Case {
    const char* name;
    void      (*fn)();
};

inline int run(std::initializer_list<Case> cases) {
    int failed = 0;
    for (const auto& c : cases) {
        std::cout << "[ RUN  ] " << c.name << '\n';
        try {
            c.fn();
            std::cout << "[  OK  ] " << c.name << '\n';
        } catch (const Failure& f) {
            std::cout << "[ FAIL ] " << c.name << " — " << f.what() << '\n';
            ++failed;
        } catch (const std::exception& e) {
            std::cout << "[ FAIL ] " << c.name << " — std::exception: " << e.what() << '\n';
            ++failed;
        } catch (...) {
            std::cout << "[ FAIL ] " << c.name << " — unknown exception\n";
            ++failed;
        }
    }
    std::cout << (failed ? "FAILURES" : "ALL PASSED") << ' ' << '(' << failed << " failed)\n";
    return failed == 0 ? 0 : 1;
}

} // namespace collapsar::test
