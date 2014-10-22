/*
Copyright (c) 2014, Dust Networks.  All rights reserved.
*/

#ifndef WELL_KNOWN_PORTS_H
#define WELL_KNOWN_PORTS_H

// range of highly compressed UDP ports
#define WKP_USER_1                0xF0B8
#define WKP_USER_2                0xF0B9
#define WKP_USER_3                0xF0BA
#define WKP_USER_4                0xF0BB
#define WKP_USER_5                0xF0BC
#define WKP_USER_6                0xF0BD
#define WKP_USER_7                0xF0BE
#define WKP_USER_8                0xF0BF
// port used by the On-chip Application Protocol (OAP)
#define WKP_OAP                   WKP_USER_2

// ports used by different sample applications
#define WKP_SPI_NET               60100
#define WKP_GPIO_NET              60101
#define WKP_DC2126A               60102
#define WKP_LIS311                60103

#endif
