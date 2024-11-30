/*BEGIN_LEGAL 
Copyright 2002-2019 Intel Corporation.

This software and the related documents are Intel copyrighted materials, and your
use of them is governed by the express license under which they were provided to
you ("License"). Unless the License provides otherwise, you may not use, modify,
copy, publish, distribute, disclose or transmit this software or the related
documents without Intel's prior written permission.

This software and the related documents are provided as is, with no express or
implied warranties, other than those that are expressly stated in the License.
END_LEGAL */
/// @file xed-extension-enum.h

// This file was automatically generated.
// Do not edit this file.

#if !defined(XED_EXTENSION_ENUM_H)
# define XED_EXTENSION_ENUM_H
#include "xed-common-hdrs.h"
typedef enum {
  XED_EXTENSION_INVALID,
  XED_EXTENSION_3DNOW,
  XED_EXTENSION_ADOX_ADCX,
  XED_EXTENSION_AES,
  XED_EXTENSION_AVX,
  XED_EXTENSION_AVX2,
  XED_EXTENSION_AVX2GATHER,
  XED_EXTENSION_AVX512EVEX,
  XED_EXTENSION_AVX512VEX,
  XED_EXTENSION_AVXAES,
  XED_EXTENSION_BASE,
  XED_EXTENSION_BMI1,
  XED_EXTENSION_BMI2,
  XED_EXTENSION_CET,
  XED_EXTENSION_CLDEMOTE,
  XED_EXTENSION_CLFLUSHOPT,
  XED_EXTENSION_CLFSH,
  XED_EXTENSION_CLWB,
  XED_EXTENSION_CLZERO,
  XED_EXTENSION_ENQCMD,
  XED_EXTENSION_F16C,
  XED_EXTENSION_FMA,
  XED_EXTENSION_FMA4,
  XED_EXTENSION_GFNI,
  XED_EXTENSION_INVPCID,
  XED_EXTENSION_LONGMODE,
  XED_EXTENSION_LZCNT,
  XED_EXTENSION_MCOMMIT,
  XED_EXTENSION_MMX,
  XED_EXTENSION_MONITOR,
  XED_EXTENSION_MONITORX,
  XED_EXTENSION_MOVBE,
  XED_EXTENSION_MOVDIR,
  XED_EXTENSION_MPX,
  XED_EXTENSION_PAUSE,
  XED_EXTENSION_PCLMULQDQ,
  XED_EXTENSION_PCONFIG,
  XED_EXTENSION_PKU,
  XED_EXTENSION_PREFETCHWT1,
  XED_EXTENSION_PT,
  XED_EXTENSION_RDPID,
  XED_EXTENSION_RDPRU,
  XED_EXTENSION_RDRAND,
  XED_EXTENSION_RDSEED,
  XED_EXTENSION_RDTSCP,
  XED_EXTENSION_RDWRFSGS,
  XED_EXTENSION_RTM,
  XED_EXTENSION_SGX,
  XED_EXTENSION_SGX_ENCLV,
  XED_EXTENSION_SHA,
  XED_EXTENSION_SMAP,
  XED_EXTENSION_SMX,
  XED_EXTENSION_SSE,
  XED_EXTENSION_SSE2,
  XED_EXTENSION_SSE3,
  XED_EXTENSION_SSE4,
  XED_EXTENSION_SSE4A,
  XED_EXTENSION_SSSE3,
  XED_EXTENSION_SVM,
  XED_EXTENSION_TBM,
  XED_EXTENSION_VAES,
  XED_EXTENSION_VIA_PADLOCK_AES,
  XED_EXTENSION_VIA_PADLOCK_MONTMUL,
  XED_EXTENSION_VIA_PADLOCK_RNG,
  XED_EXTENSION_VIA_PADLOCK_SHA,
  XED_EXTENSION_VMFUNC,
  XED_EXTENSION_VPCLMULQDQ,
  XED_EXTENSION_VTX,
  XED_EXTENSION_WAITPKG,
  XED_EXTENSION_WBNOINVD,
  XED_EXTENSION_X87,
  XED_EXTENSION_XOP,
  XED_EXTENSION_XSAVE,
  XED_EXTENSION_XSAVEC,
  XED_EXTENSION_XSAVEOPT,
  XED_EXTENSION_XSAVES,
  XED_EXTENSION_LAST
} xed_extension_enum_t;

/// This converts strings to #xed_extension_enum_t types.
/// @param s A C-string.
/// @return #xed_extension_enum_t
/// @ingroup ENUM
XED_DLL_EXPORT xed_extension_enum_t str2xed_extension_enum_t(const char* s);
/// This converts strings to #xed_extension_enum_t types.
/// @param p An enumeration element of type xed_extension_enum_t.
/// @return string
/// @ingroup ENUM
XED_DLL_EXPORT const char* xed_extension_enum_t2str(const xed_extension_enum_t p);

/// Returns the last element of the enumeration
/// @return xed_extension_enum_t The last element of the enumeration.
/// @ingroup ENUM
XED_DLL_EXPORT xed_extension_enum_t xed_extension_enum_t_last(void);
#endif
