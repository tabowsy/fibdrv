#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FIB_DEV "/dev/fibonacci"

// static inline long long consumed(struct timespec *t1, struct timespec *t2)
// {
//     return (long long) (t2->tv_sec - t1->tv_sec) * 1e9 +
//            (long long) (t2->tv_nsec - t1->tv_nsec);
// }

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

    for (int i = 0; i <= offset; i++) {
        lseek(fd, i, SEEK_SET);
        clock_gettime(CLOCK_MONOTONIC, &tt1);
        sz = read(fd, &val, sizeof(val));
        clock_gettime(CLOCK_MONOTONIC, &tt2);
        printf("Reading from " FIB_DEV " at offset %d", i);
        printf(", returned the sequence ");
        print_u128_u(val);
        printf(".\n");
        // printf("consume %lld ns\n", consumed(&tt1, &tt2));
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
