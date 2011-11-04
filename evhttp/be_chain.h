#pragma once

#ifdef BE_ECHO
#include "be_echo.h"
#endif

#ifdef BE_DNS
#include "be_dns.h"
#endif

#define BE_REGISTER(_be) \
	&_be,

struct be_oper *be_list[] = {
	#ifdef BE_ECHO
	BE_REGISTER(be_echo_oper)
	#endif

	#ifdef BE_DNS
	BE_REGISTER(be_dns_oper)
	#endif

	NULL
};
