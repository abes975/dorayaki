
#ifdef DORAYAKI_DEBUG
    /* enable assert too */
    #define NDEBUG

    #define DEBUG_MSG(fd, fmt, ...) \
        fprintf(fd, "[DEBUG] (%s: %d): " fmt "\n", __func__, __LINE__, ##__VA_ARGS__);
    #define INFO_MSG(fmt, ...) \
        fprintf(fd, "[INFO] (%s: %d): " fmt "\n", __func__, __LINE__, ##__VA_ARGS__);
#else
    #define DEBUG_MSG(fmt, ...)
    #define INFO_MSG(fmt, ...)
#endif


#define FATAL_ERROR(fd, fmt, ...) \
    {\
        fprintf(fd, "[FATAL ERROR] (%s: %s: %d): " fmt "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__); \
        exit(-2); \
    }


#define ERROR_MSG(fd, fmt, ...) \
        fprintf(fd, "[ERROR] (%s): " fmt "\n", __func__ ,##__VA_ARGS__);
