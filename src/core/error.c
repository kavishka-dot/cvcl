/**
 * @file error.c
 * @brief Error code to string mapping
 */

#include <cvcl/cvcl_error.h>

const char *cvcl_strerror(cvcl_result_t err) {
    switch (err) {
        case CVCL_OK:                return "Success";
        case CVCL_ERR_NULL_PTR:      return "NULL pointer argument";
        case CVCL_ERR_INVALID_ARG:   return "Invalid argument";
        case CVCL_ERR_ALLOC:         return "Memory allocation failed";
        case CVCL_ERR_IO:            return "I/O error";
        case CVCL_ERR_UNSUPPORTED:   return "Unsupported operation";
        case CVCL_ERR_SIZE_MISMATCH: return "Image size mismatch";
        case CVCL_ERR_DEPTH_MISMATCH:return "Pixel depth mismatch";
        case CVCL_ERR_FORMAT:        return "Unrecognized or malformed format";
        case CVCL_ERR_OVERFLOW:      return "Arithmetic or buffer overflow";
        case CVCL_ERR_INTERNAL:      return "Internal library error (bug)";
        default:                     return "Unknown error";
    }
}
