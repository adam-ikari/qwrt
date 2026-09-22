/* Qzjs.js — extension example: greet_ext 声明
 * context.c 展开 QZ_EXTENSIONS 表时引用 &greet_ext，需此 extern 声明可见
 * （经 -DQZ_EXTRA_HEADERS 强制预包含到 qzjs 库编译）。 */
#ifndef QZJS_GREET_EXT_H
#define QZJS_GREET_EXT_H

#include <qzjs/qzjs.h>

extern qz_ext_t greet_ext;

#endif
