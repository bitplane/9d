#include "../namespace.h"

#include <stdlib.h>

int platform_namespace_init(Namespace *ns) {
    const char *first = getenv("SIMPLE9P_TEST_ROOT_1");
    const char *second = getenv("SIMPLE9P_TEST_ROOT_2");

    if(!first || !second || namespace_use_synthetic(ns) < 0)
        return -1;
    if(namespace_add_root(ns, "First", first) < 0)
        return -1;
    return namespace_add_root(ns, "Second", second);
}
