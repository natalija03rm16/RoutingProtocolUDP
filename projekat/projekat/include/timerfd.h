#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/timerfd.h>

typedef struct
{
    int timer_fd;
    unsigned long long wakeups_missed;
} periodic_info;

static int make_periodic(unsigned int period, periodic_info *info, int insta)
{
    int ret;
    unsigned int ns, sec;
    int fd;
    struct itimerspec itval;

    fd = timerfd_create(CLOCK_MONOTONIC, 0);
    info->wakeups_missed = 0;
    info->timer_fd = fd;
    if (fd == -1)
	return fd;

    sec = period / 1000000;
    ns = (period - (sec * 1000000)) * 1000;
    
    if (insta)
    {
        itval.it_value.tv_sec = 0; //sec;
        itval.it_value.tv_nsec = 10000; //ns; - funkcija pocinje za 10ms, ponavlja se svakih sec,ns
    }
    else
    {
        itval.it_value.tv_sec = sec; //sec;
        itval.it_value.tv_nsec = ns; //ns; - funkcija pocinje za 10ms, ponavlja se svakih sec,ns
    }
    itval.it_interval.tv_sec = sec;
    itval.it_interval.tv_nsec = ns;
	
    ret = timerfd_settime(fd, 0, &itval, NULL);
    return ret;
}

static void wait_period(periodic_info *info)
{
    unsigned long long missed;
    int ret;

    ret = read(info->timer_fd, &missed, sizeof(missed));
    if (ret == -1) 
    {
	perror("read timer");
	return;
    }

    info->wakeups_missed += missed;
}
