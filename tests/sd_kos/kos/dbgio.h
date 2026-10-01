#ifndef TEST_SD_KOS_DBGIO_H
#define TEST_SD_KOS_DBGIO_H
const char *dbgio_dev_get(void);
int dbgio_dev_select(const char *name);
void dbgio_disable(void);
#endif
