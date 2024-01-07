#include "types.h"
#include "user.h"

int main() {
    int niceSetter[] = {-20, -5, 0, 9};
    schedlog(10000);

    for (int i = 0; i < 4; i++) {
        if (nicefork(niceSetter[i]) == 0) {
            char *argv[] = {"loop", 0};
            exec("loop", argv);
        }
    }

    for (int i = 0; i < 4; i++) {
        wait();
    }
    
    shutdown();
}