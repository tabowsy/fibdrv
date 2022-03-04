#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FIB_DEV "/dev/fibonacci"
#define FIB_SYS "/sys/kernel/fib_time/"

/* user space time */
static inline long long diff_in_ns(struct timespec *t1, struct timespec *t2)
{
    if (t2->tv_nsec - t1->tv_nsec < 0) {
        return (long long) (t2->tv_sec - t1->tv_sec - 1) * 1e9 +
               (long long) (t2->tv_nsec - t1->tv_nsec + 1e9);
    } else {
        return (long long) (t2->tv_sec - t1->tv_sec) * 1e9 +
               (long long) (t2->tv_nsec - t1->tv_nsec);
    }
}

/* kernel space time */
static long int get_ktime(int fib)
{
    int kfd;
    if (fib == 1)
        kfd = open(FIB_SYS "fib_kt_ns", O_RDWR);
    else
        kfd = open(FIB_SYS "copy_kt_ns", O_RDWR);
    if (kfd < 0) {
        perror("Failed to open sys kobject");
        exit(1);
    }
    char buf[64];
    read(kfd, buf, 64);
    close(kfd);
    return atol(buf);
}

/* print unsigned int 128 */
typedef __uint128_t uint128_t;
/*      UINT64_MAX 18446744073709551615ULL */
#define P10_UINT64 10000000000000000000ULL /* 19 zeroes */
#define E10_UINT64 19
#define STRINGIZER(x) #x
#define TO_STRING(x) STRINGIZER(x)
static int print_u128_u(uint128_t u128)
{
    int rc;
    if (u128 > UINT64_MAX) {
        uint128_t leading = u128 / P10_UINT64;
        uint64_t trailing = u128 % P10_UINT64;
        rc = print_u128_u(leading);
        rc += printf("%." TO_STRING(E10_UINT64) PRIu64, trailing);
    } else {
        uint64_t u64 = u128;
        rc = printf("%" PRIu64, u64);
    }
    return rc;
}

int main()
{
    struct timespec tt1, tt2;

    long long sz;

    uint128_t val;
    char write_buf[] = "testing writing";
    int offset = 100; /* TODO: try test something bigger than the limit */

    int fd = open(FIB_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }

    for (int i = 0; i <= offset; i++) {
        sz = write(fd, write_buf, strlen(write_buf));
        printf("Writing to " FIB_DEV ", returned the sequence %lld\n", sz);
    }

    /* call before counter to prevent page fault */
    clock_gettime(CLOCK_MONOTONIC, &tt1);
    read(fd, &val, sizeof(val));

    for (int i = 0; i <= offset; i++) {
        lseek(fd, i, SEEK_SET);
        clock_gettime(CLOCK_MONOTONIC, &tt1);
        sz = read(fd, &val, sizeof(val));
        clock_gettime(CLOCK_MONOTONIC, &tt2);
        long int kernel_time =
            get_ktime(1) + get_ktime(0);  // get kernel fib and copy time
        long int total_time = diff_in_ns(&tt1, &tt2);
        printf(
            "[Offset %d] total time: %ld, kernel time: %ld, user time: %ld\n",
            i, total_time, kernel_time, total_time - kernel_time);

        printf("Reading from " FIB_DEV " at offset %d", i);
        printf(", returned the sequence ");
        print_u128_u(val);
        printf(".\n");
    }

    for (int i = offset; i >= 0; i--) {
        lseek(fd, i, SEEK_SET);
        sz = read(fd, &val, sizeof(val));
        printf("Reading from " FIB_DEV " at offset %d", i);
        printf(", returned the sequence ");
        print_u128_u(val);
        printf(".\n");
    }

    close(fd);
    return 0;
}
