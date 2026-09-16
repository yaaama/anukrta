#ifndef FOURCC_H_
#define FOURCC_H_

#include <stdint.h>

#ifndef __has_attribute
#  define __has_attribute(attr) 0
#endif

#if __has_attribute(const)
#  define __AK_4CC_CONST_ATTR __attribute__((const))
#else
#  define __AK_4CC_CONST_ATTR
#endif

/* Length Macros */
#define AK_VIDEO_EXT_MAX_LEN 4
#define AK_VIDEO_EXT_MIN_LEN 2

/* Helper Macros for Prefixing */
#define __AK_4CC_GLUE(a, b) a##b
#define __AK_4CC_JOIN(a, b) __AK_4CC_GLUE(a, b)
#define __AK_4CC_PREFIX AK_EXT_4CC_

#define AK_4CC_MAKE(a, b, c, d) \
  ((uint32_t) (a) | ((uint32_t) (b) << 8) | ((uint32_t) (c) << 16) | ((uint32_t) (d) << 24))

#define AK_VID_EXT_MAX_LEN 4
#define AK_VID_EXT_MIN_LEN 2

#define AK_VIDEO_EXT_TABLE                    \
  X(3G2, '3', 'g', '2', ' ', "3g2", "3G2")    \
  X(3GP, '3', 'g', 'p', ' ', "3gp", "3GP")    \
  X(AMV, 'a', 'm', 'v', ' ', "amv", "AMV")    \
  X(ASF, 'a', 's', 'f', ' ', "asf", "ASF")    \
  X(AVI, 'a', 'v', 'i', ' ', "avi", "AVI")    \
  X(F4A, 'f', '4', 'a', ' ', "f4a", "F4A")    \
  X(F4B, 'f', '4', 'b', ' ', "f4b", "F4B")    \
  X(F4P, 'f', '4', 'p', ' ', "f4p", "F4P")    \
  X(F4V, 'f', '4', 'v', ' ', "f4v", "F4V")    \
  X(FLV, 'f', 'l', 'v', ' ', "flv", "FLV")    \
  X(GIFV, 'g', 'i', 'f', 'v', "gifv", "GIFV") \
  X(M4P, 'm', '4', 'p', ' ', "m4p", "M4P")    \
  X(M4V, 'm', '4', 'v', ' ', "m4v", "M4V")    \
  X(MKV, 'm', 'k', 'v', ' ', "mkv", "MKV")    \
  X(MNG, 'm', 'n', 'g', ' ', "mng", "MNG")    \
  X(MOD, 'm', 'o', 'd', ' ', "mod", "MOD")    \
  X(MOV, 'm', 'o', 'v', ' ', "mov", "MOV")    \
  X(MP2, 'm', 'p', '2', ' ', "mp2", "MP2")    \
  X(MP4, 'm', 'p', '4', ' ', "mp4", "MP4")    \
  X(MPE, 'm', 'p', 'e', ' ', "mpe", "MPE")    \
  X(MPEG, 'm', 'p', 'e', 'g', "mpeg", "MPEG") \
  X(MPG, 'm', 'p', 'g', ' ', "mpg", "MPG")    \
  X(MPV, 'm', 'p', 'v', ' ', "mpv", "MPV")    \
  X(MXF, 'm', 'x', 'f', ' ', "mxf", "MXF")    \
  X(NSV, 'n', 's', 'v', ' ', "nsv", "NSV")    \
  X(OGG, 'o', 'g', 'g', ' ', "ogg", "OGG")    \
  X(OGV, 'o', 'g', 'v', ' ', "ogv", "OGV")    \
  X(QT, 'q', 't', ' ', ' ', "qt", "QT")       \
  X(RM, 'r', 'm', ' ', ' ', "rm", "RM")       \
  X(ROQ, 'r', 'o', 'q', ' ', "roq", "ROQ")    \
  X(RRC, 'r', 'r', 'c', ' ', "rrc", "RRC")    \
  X(SVI, 's', 'v', 'i', ' ', "svi", "SVI")    \
  X(VOB, 'v', 'o', 'b', ' ', "vob", "VOB")    \
  X(WEBM, 'w', 'e', 'b', 'm', "webm", "WEBM") \
  X(WMV, 'w', 'm', 'v', ' ', "wmv", "WMV")    \
  X(YUV, 'y', 'u', 'v', ' ', "yuv", "YUV")

/* Generate the Enum */
#define X(id, c1, c2, c3, c4, lower, upper) \
  __AK_4CC_JOIN(__AK_4CC_PREFIX, id) = AK_4CC_MAKE(c1, c2, c3, c4),

typedef enum { AK_VIDEO_EXT_TABLE } AK_VIDEO_4CC;

#undef X

/*
 * Compile-time counting trick employed here
 * We define X to just output "+ 1".
 * The macro expands to (0 + 1 + 1 + 1...)
 */
#define X(id, c1, c2, c3, c4, lower, upper) (+1)
#define AK_VIDEO_EXT_COUNT (0 AK_VIDEO_EXT_TABLE)
#undef X

/*
 * Struct definition for an extension.
 */
typedef struct ak_ext_info {
  char lower[5];
  char upper[5];
  unsigned char _padding[2];
  AK_VIDEO_4CC fourcc;
} ak_ext_info;

static inline __AK_4CC_CONST_ATTR int anu_4cc_is_valid (const uint32_t fourcc_code) {
  switch (fourcc_code) {
    /* Define X to build the case statements: */
#define X(id, c1, c2, c3, c4, lower, upper) case __AK_4CC_JOIN(__AK_4CC_PREFIX, id):

    AK_VIDEO_EXT_TABLE

#undef X /* Undefine X so it doesn't leak out */
    return 1;

    /* Return 0 (false) if it does not match any extension */
    default:
      return 0;
  }
}

#undef __AK_4CC_CONST_ATTR
#undef __AK_4CC_GLUE
#undef __AK_4CC_JOIN
#undef __AK_4CC_PREFIX

#endif  // FOURCC_H_
