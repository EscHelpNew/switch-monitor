#ifndef LAB_SLEEP_POLICY_H
#define LAB_SLEEP_POLICY_H
/* Uses the already-initialized libnx applet service. Never changes a persistent
 * console setting; restore the observed per-application policy on normal exit. */
typedef struct {bool saved,previous;} LabSleepGuard;
static inline void lab_sleep_begin(LabSleepGuard *guard)
{
    guard->saved=false;
    if(R_SUCCEEDED(appletIsAutoSleepDisabled(&guard->previous)))
        guard->saved=R_SUCCEEDED(appletSetAutoSleepDisabled(true));
}
static inline void lab_sleep_end(LabSleepGuard *guard)
{
    if(guard->saved){appletSetAutoSleepDisabled(guard->previous);guard->saved=false;}
}
#endif
