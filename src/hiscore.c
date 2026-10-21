/* The high score on the USB drive */

#include <fcntl.h>
#include <unistd.h>
#include "hiscore.h"
#include "log.h"

unsigned load_best(void)
{
    unsigned char buf[2];
    unsigned best = 0;
    int fd = open(HISCORE_FILE, O_RDONLY);
    if (fd < 0)
        return 0; /* no file yet, so the high score is 0 */
    if (read(fd, buf, 2) == 2)
        best = buf[0] | ((unsigned)buf[1] << 8);
    close(fd);
    return best;
}

void save_best(unsigned best)
{
    unsigned char buf[2];
    int fd = open(HISCORE_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
    {
        log_error("could not open " HISCORE_FILE);
        return;
    }
    buf[0] = best & 0xFF;
    buf[1] = best >> 8;
    if (write(fd, buf, 2) != 2)
        log_error("could not write " HISCORE_FILE);
    close(fd);
}
