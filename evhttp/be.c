#include "be.h"
#include <stdio.h>

extern struct be_oper *be_list[];

BE_STATUS be_init_all()
{
	if (be_list[0] == NULL) {
		printf("be: init failed: no backend available\n");
		return BE_ERROR;
	}

	struct be_oper **it;
	for (it = be_list; *it; ++it) {
		struct be_oper *be = *it;
		if (be->init(be) == BE_ERROR) {
			printf("be: %s failed\n", be->name);
			while (--it != be_list) {
				(*it)->finish(*it);
				printf("be: finish[%s] \n", be->name);
			}
			return BE_ERROR;
		}
		printf("be: init[%s] \n", be->name);
	}
	return BE_OK;
}
