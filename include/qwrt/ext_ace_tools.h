/*
 * qwrt ACE Tools Extension — Public Header
 *
 * Declares the ace_tools extension that provides native tool implementations
 * for the ACE agent engine: aceRead, aceWrite, aceEdit, aceGlob, aceGrep,
 * aceBash, aceExists.
 *
 * When QWRT_WITH_ACE_TOOLS is defined, the extension registers these
 * functions on the pal object during context creation. Otherwise it is
 * a no-op.
 */

#ifndef QWRT_EXT_ACE_TOOLS_H
#define QWRT_EXT_ACE_TOOLS_H

#include "qwrt.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ACE Tools extension definition.
 *
 * Register via qwrt_config_t.extensions or let context.c auto-register it.
 * The init hook adds pal.aceRead/Write/Edit/Glob/Grep/Bash/Exists.
 */
extern const qwrt_ext_t qwrt_ace_tools_ext;

#ifdef __cplusplus
}
#endif

#endif /* QWRT_EXT_ACE_TOOLS_H */