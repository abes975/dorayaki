
#ifdef DORAYAKI_DEBUG
    /* enable assert too */
    #define NDEBUG

    #define DEBUG_MSG(fmt, ...) \
        printf("[DEBUG] (%s: %d): " fmt "\n", __func__, __LINE__, ##__VA_ARGS__);
    #define INFO_MSG(fmt, ...) \
        printf("[INFO] (%s: %d): " fmt "\n", __func__, __LINE__, ##__VA_ARGS__);
#else
    #define DEBUG_MSG(fmt, ...)
    #define INFO_MSG(fmt, ...)
#endif


#define FATAL_ERROR(fmt, ...) \
    {\
        printf("[FATAL ERROR] (%s: %s: %d): " fmt "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__); \
        exit(-2); \
    }


