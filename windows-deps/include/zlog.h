/* Minimal zlog stub header */
#ifndef ZLOG_H
#define ZLOG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zlog_category zlog_category_t;

/* Stub function declarations */
int zlog_init(const char *confpath);
void zlog_fini(void);
zlog_category_t* zlog_get_category(const char *cname);
int zlog_info(zlog_category_t *category, const char *format, ...);
int zlog_error(zlog_category_t *category, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* ZLOG_H */
