/* Table UTF-8 stricte de msg_utf8_valid : cas valides/invalides, dont les cas de Markus Kuhn. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "proto/msg/msg.h"
#include "test.h"

struct utf8_case {
    const char *s;
    size_t n;
    int ok;
};

static const struct utf8_case CASES[] = {
    {"", 0, 1},
    {"\x00", 1, 1},
    {"a", 1, 1},
    {"\x7F", 1, 1},
    {"\xC2\x80", 2, 1},
    {"\xDF\xBF", 2, 1},
    {"\xE0\xA0\x80", 3, 1},
    {"\xED\x9F\xBF", 3, 1},
    {"\xEE\x80\x80", 3, 1},
    {"\xEF\xBF\xBD", 3, 1},
    {"\xEF\xBF\xBE", 3, 1},
    {"\xEF\xBF\xBF", 3, 1},
    {"\xF0\x90\x80\x80", 4, 1},
    {"\xF1\x80\x80\xBF", 4, 1},
    {"\xF4\x8F\xBF\xBF", 4, 1},
    {"\xCE\xBA\xCF\x8C\xCF\x83\xCE\xBC\xCE\xB5", 10, 1},
    {"a\xE2\x82\xAC\xF0\x9D\x84\x9E", 8, 1},
    {"\xC0\x80", 2, 0},
    {"\xC1\xBF", 2, 0},
    {"\xE0\x80\x80", 3, 0},
    {"\xE0\x9F\xBF", 3, 0},
    {"\xE0\x80\xAF", 3, 0},
    {"\xC0\xAF", 2, 0},
    {"\xF0\x80\x80\x80", 4, 0},
    {"\xF0\x8F\xBF\xBF", 4, 0},
    {"\xED\xA0\x80", 3, 0},
    {"\xED\xBF\xBF", 3, 0},
    {"\xED\xA0\x80\xED\xB0\x80", 6, 0},
    {"\xF4\x90\x80\x80", 4, 0},
    {"\xF5\x80\x80\x80", 4, 0},
    {"\xFF", 1, 0},
    {"\xFE", 1, 0},
    {"\x80", 1, 0},
    {"\xBF", 1, 0},
    {"\xC2", 1, 0},
    {"\xE2\x82", 2, 0},
    {"\xF0\x90\x80", 3, 0},
    {"a\xE2\x82", 3, 0},
    {"\xF8\x88\x80\x80\x80", 5, 0},
    {"\xFC\x84\x80\x80\x80\x80", 6, 0},
    {"\xC2\x41", 2, 0},
    {"\xE2\x28\xA1", 3, 0},
    {"\xF0\x90\x80\x41", 4, 0},
    {"\x00\xC0", 2, 0},
};

static void test_table(void)
{
    size_t i;
    size_t count = sizeof CASES / sizeof CASES[0];

    for (i = 0; i < count; i++) {
        const struct utf8_case *c = &CASES[i];
        int got = msg_utf8_valid((const uint8_t *)c->s, c->n);

        if (got != c->ok) {
            fprintf(stderr, "cas UTF-8 %zu : attendu %d, obtenu %d\n", i, c->ok, got);
            ++*test_fail_counter();
        }
    }
    CHECK(count >= 20);
}

static void test_null_and_empty(void)
{
    CHECK_EQ_INT(msg_utf8_valid(NULL, 0), 1);
    CHECK_EQ_INT(msg_utf8_valid(NULL, 1), 0);
}

int main(void)
{
    RUN(test_table);
    RUN(test_null_and_empty);
    return test_summary();
}
