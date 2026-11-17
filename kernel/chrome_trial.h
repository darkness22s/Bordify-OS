#ifndef BORDIFY_CHROME_TRIAL_H
#define BORDIFY_CHROME_TRIAL_H
/*
 * Isolated, opt-in compatibility experiment.
 * Never part of the normal release boot unless CHROME_PROBE=1.
 * Read-only Chrome package inspection and Windows executable load attempt.
 */
void chrome_trial_run(void);
#endif
