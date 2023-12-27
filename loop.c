#include "types.h"
#include "user.h"

#define CPU_BURST 4e9

int main() {
    int dummy = 0;
    for (unsigned int i = 0; i < CPU_BURST; i++) {
        dummy += i;
    }

    exit();
}