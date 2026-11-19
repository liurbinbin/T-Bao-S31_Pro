/* SPDX-License-Identifier: Apache-2.0 */
#include "app_pairing_code.h"

#include <stddef.h>
#include <string.h>

static const char *find_ascii_case_insensitive(const char *text, const char *word)
{
    for (; *text != '\0'; ++text) {
        size_t i = 0;
        while (word[i] != '\0' && text[i] != '\0') {
            char ch = text[i];
            if (ch >= 'A' && ch <= 'Z') {
                ch += 'a' - 'A';
            }
            if (ch != word[i]) {
                break;
            }
            ++i;
        }
        if (word[i] == '\0') {
            return text + i;
        }
    }
    return NULL;
}

static size_t read_digit(const char *text, char *digit)
{
    if (*text >= '0' && *text <= '9') {
        *digit = *text;
        return 1;
    }
    static const char *const chinese_digits[] = {
        "零", "一", "二", "三", "四", "五", "六", "七", "八", "九",
        "〇", "幺", "两",
    };
    static const char values[] = "0123456789012";
    for (size_t i = 0; i < sizeof(chinese_digits) / sizeof(chinese_digits[0]); ++i) {
        size_t len = strlen(chinese_digits[i]);
        if (strncmp(text, chinese_digits[i], len) == 0) {
            *digit = values[i];
            return len;
        }
    }
    return 0;
}

static size_t read_separator(const char *text)
{
    if (*text != '\0' && strchr(" \t\r\n,-:", *text) != NULL) {
        return 1;
    }
    static const char *const separators[] = {"，", "、", "："};
    for (size_t i = 0; i < sizeof(separators) / sizeof(separators[0]); ++i) {
        size_t len = strlen(separators[i]);
        if (strncmp(text, separators[i], len) == 0) {
            return len;
        }
    }
    return 0;
}

bool app_pairing_code_extract(const char *text,
                             char code[APP_PAIRING_CODE_DIGITS + 1])
{
    if (text == NULL || code == NULL) {
        return false;
    }
    code[0] = '\0';
    static const char *const markers[] = {
        "验证码", "激活码", "配对码", "verification code", "activation code",
        "pairing code", "binding code",
    };
    const char *start = NULL;
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i) {
        const char *match = find_ascii_case_insensitive(text, markers[i]);
        if (match != NULL && (start == NULL || match < start)) {
            start = match;
        }
    }
    if (start == NULL) {
        return false;
    }

    char candidate[APP_PAIRING_CODE_DIGITS + 1] = {0};
    size_t count = 0;
    for (const char *p = start; *p != '\0';) {
        char digit;
        size_t len = read_digit(p, &digit);
        if (len != 0) {
            if (count < APP_PAIRING_CODE_DIGITS) {
                candidate[count] = digit;
            }
            /* 第七位后保持无效，不能截取长数字串的前六位。 */
            if (count <= APP_PAIRING_CODE_DIGITS) {
                ++count;
            }
            p += len;
            continue;
        }
        len = read_separator(p);
        if (len != 0) {
            p += len;
            continue;
        }
        if (count == APP_PAIRING_CODE_DIGITS) {
            memcpy(code, candidate, sizeof(candidate));
            return true;
        }
        count = 0;
        ++p;
    }
    if (count == APP_PAIRING_CODE_DIGITS) {
        memcpy(code, candidate, sizeof(candidate));
        return true;
    }
    return false;
}
