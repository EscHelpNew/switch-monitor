#ifndef GAME_RELOAD_REPORTS_H
#define GAME_RELOAD_REPORTS_H
#include <stddef.h>
size_t reload_crash_reports(char *,size_t,const char *);
int reload_trace(const char *,const char *,unsigned,int);
#endif
