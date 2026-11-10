#ifndef OWNERSHIP_H
#define OWNERSHIP_H

/*
 * chown and chgrp. Every file on Windows is shown as owned by the current user and its primary group (stat does the
 * same), so naming that owner or group changes nothing ("retained") and naming another one is refused, as it is for a
 * user who may not give a file away: "changing ownership of 'f': Operation not permitted".
 */

/** The main of chown (isGroup 0) or of chgrp (isGroup 1). */
int ownership_main(int argc, char* argv[], int isGroup);

#endif //OWNERSHIP_H
