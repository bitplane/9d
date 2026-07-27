#include "namespace.h"

int platform_namespace_init(Namespace *ns) {
    return namespace_use_native(ns, "/");
}
