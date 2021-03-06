/* SPDX-FileCopyrightText: 2021 Pali Rohár <pali@kernel.org> */
/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h> /* for open() */
#include <unistd.h> /* for close() */
#include <sys/types.h> /* for O_* */
#include <sys/ioctl.h> /* for ioctl() */

#include <asm/ioctls.h> /* for TCGETS, TCSETS, TCGETS2, TCSETS2, TIOCGSERIAL, TIOCSSERIAL */
#include <asm/termbits.h> /* for BOTHER, Bnnn, struct termios, struct termios2 */
#include <linux/serial.h> /* for ASYNC_SPD_*, struct serial_struct */

#define B(n) { B##n, n }
static struct { tcflag_t bn; unsigned int n; }
map[] =
{
    B(0), B(50), B(75), B(110), B(134), B(150), B(200), B(300), B(600),
    B(1200), B(1800), B(2400), B(4800), B(9600), B(19200), B(38400),
    B(57600), B(115200), B(230400), B(460800), B(500000), B(576000),
    B(921600), B(1000000), B(1152000), B(1500000), B(2000000),
#ifdef B2500000
    /* non-SPARC architectures support these Bnnn constants */
    B(2500000), B(3000000), B(3500000), B(4000000)
#else
    /* SPARC architecture supports these Bnnn constants */
    B(76800), B(153600), B(307200), B(614400)
#endif
};
#undef B

static tcflag_t
map_n_to_bn(unsigned int n)
{
    size_t i;

    for (i = 0; i < sizeof(map)/sizeof(map[0]); i++) {
        if (map[i].n == n)
            return map[i].bn;
    }

    return B0;
}

#ifndef BOTHER
static unsigned int
map_bn_to_n(tcflag_t bn)
{
    size_t i;

    for (i = 0; i < sizeof(map)/sizeof(map[0]); i++) {
        if (map[i].bn == bn)
            return map[i].n;
    }

    return (unsigned int)-1;
}
#endif

static unsigned int
get_spd_B38400_alias(int fd)
{
    struct serial_struct ser;
    int rc;

    rc = ioctl(fd, TIOCGSERIAL, &ser);
    if (rc)
        return 38400; /* ASYNC_SPD_MASK is unsupported */

    if (!(ser.flags & ASYNC_SPD_MASK))
        return 38400; /* ASYNC_SPD_MASK is not set */

    if ((ser.flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST && !ser.custom_divisor)
        return 38400; /* ASYNC_SPD_CUST is not active */

    if ((ser.flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
        return 56000;
    else if ((ser.flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
        return 115200;
    else if ((ser.flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
        return 230400;
    else if ((ser.flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
        return 460800;
    else if ((ser.flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST)
        return (ser.baud_base + ser.custom_divisor/2) / ser.custom_divisor;
    else
        return (unsigned int)-1;
}

int
main(int argc, char *argv[])
{
    /* Declare tio structure, its type depends on supported ioctl */
#ifdef TCGETS2
    struct termios2 tio;
#else
    struct termios tio;
#endif
    struct serial_struct ser;
    unsigned int n;
    tcflag_t bn;
    int fd, rc;

    if (argc != 2 && argc != 3 && argc != 4) {
        fprintf(stderr, "Usage: %s device [output [input]]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    fd = open(argv[1], O_RDWR | O_NONBLOCK | O_NOCTTY);
    if (fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    /* Get the current serial port settings via supported ioctl */
#ifdef TCGETS2
    rc = ioctl(fd, TCGETS2, &tio);
#else
    rc = ioctl(fd, TCGETS, &tio);
#endif
    if (rc) {
        perror("TCGETS");
        close(fd);
        exit(EXIT_FAILURE);
    }

    /* Change baud rate when more arguments were provided */
    if (argc == 3 || argc == 4) {
        /* Clear the current output baud rate and fill a new value */
        n = atoi(argv[2]);
        /* When possible prefer usage of Bnnn constant as glibc-based
           applications are not able to parse BOTHER c_ospeed baud rate */
        bn = map_n_to_bn(n);
        if (n != 0 && bn == B0) {
#ifdef BOTHER
            bn = BOTHER;
#else
            rc = ioctl(fd, TIOCGSERIAL, &ser);
            if (rc) {
                fprintf(stderr, "baud rate %u is unsupported\n", n);
                close(fd);
                exit(EXIT_FAILURE);
            }
            /* B38400 is aliased to different baud rate configured by
               custom_divisor field when ASYNC_SPD_MASK flag is set to
               ASYNC_SPD_CUST value via TIOCSSERIAL */
            bn = B38400;
            ser.flags &= ~ASYNC_SPD_MASK;
            ser.flags |= ASYNC_SPD_CUST;
            ser.custom_divisor = (ser.baud_base + n/2) / n;
            rc = ioctl(fd, TIOCSSERIAL, &ser);
            if (rc) {
                perror("TIOCSSERIAL");
                close(fd);
                exit(EXIT_FAILURE);
            }
#endif
        } else if (n == 38400) {
            rc = ioctl(fd, TIOCGSERIAL, &ser);
            if (!rc && (ser.flags & ASYNC_SPD_MASK)) {
                /* Clear ASYNC_SPD_MASK flag via TIOCSSERIAL
                   as it aliases 38400 to some other baud rate */
                ser.flags &= ~ASYNC_SPD_MASK;
                ser.custom_divisor = 0;
                rc = ioctl(fd, TIOCSSERIAL, &ser);
                if (rc) {
                    perror("TIOCSSERIAL");
                    close(fd);
                    exit(EXIT_FAILURE);
                }
            }
        }
        tio.c_cflag &= ~CBAUD;
        tio.c_cflag |= bn;
#ifdef BOTHER
        tio.c_ospeed = n;
#endif

        /* When 4th argument is not provided reuse output baud rate */
        if (argc == 4) {
            n = atoi(argv[3]);
            bn = map_n_to_bn(n);
            if (n != 0 && bn == B0) {
#ifdef BOTHER
                bn = BOTHER;
#else
                if (n != (unsigned int)atoi(argv[2])) {
                    fprintf(stderr, "input baud rate %u is unsupported\n", n);
                    close(fd);
                    exit(EXIT_FAILURE);
                }
                /* Set B38400 for ASYNC_SPD_CUST */
                bn = B38400;
#endif
            }
            if ((tio.c_cflag & CBAUD) != B0 && n == 0) {
#ifdef BOTHER
                bn = BOTHER;
#else
                fprintf(stderr, "input baud rate cannot be zero\n");
                close(fd);
                exit(EXIT_FAILURE);
#endif
            }
        }

        /* Clear the current input baud rate and fill a new value */
        if ((tio.c_cflag & CBAUD) != bn
#ifdef BOTHER
            || (bn == BOTHER && tio.c_ospeed != n)
#endif
           ) {
#ifdef IBSHIFT
            tio.c_cflag &= ~(CBAUD << IBSHIFT);
            tio.c_cflag |= bn << IBSHIFT;
#ifdef BOTHER
            tio.c_ispeed = n;
#endif
#else
            fprintf(stderr, "split baud rates are unsupported\n");
            close(fd);
            exit(EXIT_FAILURE);
#endif
        } else {
#ifdef IBSHIFT
            /* B0 sets the input baud rate to the output baud rate */
            tio.c_cflag &= ~(CBAUD << IBSHIFT);
            tio.c_cflag |= B0 << IBSHIFT;
#ifdef BOTHER
            tio.c_ispeed = 0;
#endif
#endif
        }

        /* Set new serial port settings via supported ioctl */
#ifdef TCSETS2
        rc = ioctl(fd, TCSETS2, &tio);
#else
        rc = ioctl(fd, TCSETS, &tio);
#endif
        if (rc) {
            perror("TCSETS");
            close(fd);
            exit(EXIT_FAILURE);
        }

        /* And get new values which were really configured */
#ifdef TCGETS2
        rc = ioctl(fd, TCGETS2, &tio);
#else
        rc = ioctl(fd, TCGETS, &tio);
#endif
        if (rc) {
            perror("TCGETS");
            close(fd);
            exit(EXIT_FAILURE);
        }
    }

    /* Field c_ospeed is always filled by kernel with exact baud rate value,
       kernel tries to round c_ospeed to some Bnnn constant in 2% tolerance,
       if it is not possible then BOTHER is set */
    bn = tio.c_cflag & CBAUD;
#ifdef BOTHER
    n = tio.c_ospeed;
#else
    n = map_bn_to_n(bn);
#endif
    /* B38400 can be aliased by ASYNC_SPD_MASK flag */
    if (bn == B38400)
        n = get_spd_B38400_alias(fd);
    printf("output baud rate: ");
    if (n != (unsigned int)-1)
        printf("%u\n", n);
    else
        printf("unknown\n");

#ifdef IBSHIFT
    bn = (tio.c_cflag >> IBSHIFT) & CBAUD;
    /* B0 indicates that input baud rate is set to the output baud rate */
    if (bn == B0)
#endif
        bn = tio.c_cflag & CBAUD;
#ifdef BOTHER
    n = tio.c_ispeed;
#else
    n = map_bn_to_n(bn);
#endif
    /* B38400 can be aliased by ASYNC_SPD_MASK flag */
    if (bn == B38400)
        n = get_spd_B38400_alias(fd);
    printf("input baud rate: ");
    if (n != (unsigned int)-1)
        printf("%u\n", n);
    else
        printf("unknown\n");

    close(fd);
    exit(EXIT_SUCCESS);
}
