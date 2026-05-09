/**
 * @file cvcl_version.h
 * @brief Version constants for CVCL
 */

#ifndef CVCL_VERSION_H
#define CVCL_VERSION_H

#define CVCL_VERSION_MAJOR 2
#define CVCL_VERSION_MINOR 0
#define CVCL_VERSION_PATCH 0

#define CVCL_VERSION_STRING "2.0.0"

/** Encoded as 0xMMmmpp for easy numeric comparison */
#define CVCL_VERSION_NUMBER \
    ((CVCL_VERSION_MAJOR << 16) | (CVCL_VERSION_MINOR << 8) | CVCL_VERSION_PATCH)

#define CVCL_VERSION_AT_LEAST(major, minor, patch) \
    (CVCL_VERSION_NUMBER >= (((major) << 16) | ((minor) << 8) | (patch)))

#endif /* CVCL_VERSION_H */
