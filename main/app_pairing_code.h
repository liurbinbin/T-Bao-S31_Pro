/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_PAIRING_CODE_DIGITS 6

/* 仅提取带配对语义的六位验证码，保留前导零；支持逐字中文数字。 */
bool app_pairing_code_extract(const char *text,
                             char code[APP_PAIRING_CODE_DIGITS + 1]);

#ifdef __cplusplus
}
#endif
